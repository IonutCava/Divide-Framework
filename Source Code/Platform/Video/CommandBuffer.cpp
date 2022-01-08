#include "stdafx.h"

#include "Headers/CommandBuffer.h"
#include "Platform/Video/Headers/Pipeline.h"
#include "Platform/Video/Buffers/VertexBuffer/Headers/VertexDataInterface.h"
#include "Platform/Video/Textures/Headers/Texture.h"

namespace Divide {

namespace GFX {

namespace {
    [[nodiscard]] bool ShouldSkipType(const U8 typeIndex) noexcept {
        switch (static_cast<CommandType>(typeIndex)) {
            case CommandType::BEGIN_DEBUG_SCOPE:
            case CommandType::END_DEBUG_SCOPE:
            case CommandType::ADD_DEBUG_MESSAGE:
                return true;
            default: break;
        }
        return false;
    }

    [[nodiscard]] bool IsCameraCommand(const U8 typeIndex) noexcept {
        switch (static_cast<CommandType>(typeIndex)) {
            case CommandType::PUSH_CAMERA:
            case CommandType::POP_CAMERA:
            case CommandType::SET_CAMERA:
                return true;
            default: break;
        }
        return false;
    }

    [[nodiscard]] bool DoesNotAffectRT(const U8 typeIndex) noexcept {
        if (ShouldSkipType(typeIndex) || IsCameraCommand(typeIndex)) {
            return true;
        }

        switch (static_cast<CommandType>(typeIndex)) {
            case CommandType::SET_VIEWPORT:
            case CommandType::PUSH_VIEWPORT:
            case CommandType::POP_VIEWPORT:
            case CommandType::SET_SCISSOR:
            case CommandType::SET_CAMERA:
            case CommandType::SET_CLIP_PLANES:
            case CommandType::SEND_PUSH_CONSTANTS:
            case CommandType::SET_CLIPING_STATE:
            case CommandType::SET_TEXTURE_RESIDENCY:
                return true;
            default: break;
        }
        return false;
    }

    FORCE_INLINE [[nodiscard]] bool RemoveEmptyDrawCommands(DrawCommand::CommandContainer& commands) {
        return dvd_erase_if(commands, [](const GenericDrawCommand& cmd) noexcept { return cmd._drawCount == 0u; });
    };
};

void CommandBuffer::add(const CommandBuffer& other) {
    OPTICK_EVENT();

    static_assert(sizeof(PolyContainerEntry) == 4, "PolyContainerEntry has the wrong size!");
    _commands.reserveAdditional(other._commands);

    for (const CommandEntry& cmd : other._commandOrder) {
        other.get<CommandBase>(cmd)->addToBuffer(this);
    }
    _batched = false;
}

void CommandBuffer::add(const CommandBuffer** const buffers, const size_t count) {
    OPTICK_EVENT();
    assert(buffers != nullptr);

    static_assert(sizeof(PolyContainerEntry) == 4, "PolyContainerEntry has the wrong size!");
    for (size_t i = 0; i < count; ++i) {
        add(*buffers[i]);
    }
}

void CommandBuffer::batch() {
    OPTICK_EVENT("BATCHING COMMANDS");

    if (_batched) {
        return;
    }

    clean();

    CommandBase* prevCommand = nullptr;

    const auto EraseEmptyCommands = [](CommandOrderContainer& commandOrder) {
        const size_t initialSize = commandOrder.size();
        erase_if(commandOrder, [](const CommandEntry& entry) noexcept { return entry._data == PolyContainerEntry::INVALID_ENTRY_ID; });
        return initialSize != commandOrder.size();
    };

    do {
        OPTICK_EVENT("TRY_MERGE_LOOP");

        bool tryMerge = true;
        // Try and merge ONLY descriptor sets as these don't care about commands between them (they only set global state)
        while (tryMerge) {
            OPTICK_EVENT("TRY_MERGE_LOOP_STEP_1");

            tryMerge = false;
            prevCommand = nullptr;
            for (CommandEntry& entry : _commandOrder) {
                if (entry._data != PolyContainerEntry::INVALID_ENTRY_ID && !ShouldSkipType(entry._typeIndex)) {
                    const CommandType cmdType = static_cast<CommandType>(entry._typeIndex);
                    if (cmdType == CommandType::BIND_DESCRIPTOR_SETS) {
                        CommandBase* crtCommand = get<CommandBase>(entry);

                        bool skip = false;
                        if (entry._typeIndex == to_base(CommandType::BIND_PIPELINE)) {
                            skip = reinterpret_cast<BindDescriptorSetsCommand*>(crtCommand)->_set._textureData.hasBindlessTextures();
                        }
                        if (prevCommand != nullptr && !skip && tryMergeCommands(cmdType, prevCommand, crtCommand)) {
                            --_commandCount[entry._typeIndex];
                            entry._data = PolyContainerEntry::INVALID_ENTRY_ID;
                            tryMerge = true;
                        } else {
                            prevCommand = crtCommand;
                        }
                    }
                }
            }
        }

        tryMerge = true;
        while (tryMerge) {
            OPTICK_EVENT("TRY_MERGE_LOOP_STEP_2");

            tryMerge = false;
            prevCommand = nullptr;
            CommandType prevType = CommandType::COUNT;
            for (CommandEntry& entry : _commandOrder) {
                if (entry._data != PolyContainerEntry::INVALID_ENTRY_ID && !ShouldSkipType(entry._typeIndex)) {
                    const CommandType cmdType = static_cast<CommandType>(entry._typeIndex);
                    CommandBase* crtCommand = get<CommandBase>(entry);

                    if (prevCommand != nullptr && prevType == cmdType && tryMergeCommands(cmdType, prevCommand, crtCommand)) {
                        --_commandCount[entry._typeIndex];
                        entry._data = PolyContainerEntry::INVALID_ENTRY_ID;
                        tryMerge = true;
                    } else {
                        prevType = cmdType;
                        prevCommand = crtCommand;
                    }
                }
            }
        }
    } while (EraseEmptyCommands(_commandOrder));

    // Now try and merge ONLY End/Begin render pass (don't unbind a render target if we immediately bind a new one
    prevCommand = nullptr;
    for (const CommandEntry& entry : _commandOrder) {
        if (entry._data != PolyContainerEntry::INVALID_ENTRY_ID && !DoesNotAffectRT(entry._typeIndex)) {
            CommandBase* crtCommand = get<CommandBase>(entry);
            if (prevCommand != nullptr && entry._typeIndex == to_base(CommandType::BEGIN_RENDER_PASS)) {
                static_cast<EndRenderPassCommand*>(prevCommand)->_setDefaultRTState = false;
                prevCommand = nullptr;
            } else if (entry._typeIndex == to_base(CommandType::END_RENDER_PASS)) {
                prevCommand = crtCommand;
            } else {
                prevCommand = nullptr;
            }
        }
    }

    // If we don't have any actual work to do, clear everything
    bool hasWork = false;
    for (const CommandEntry& cmd : _commandOrder) {
        if (hasWork) {
            break;
        }

        switch (static_cast<CommandType>(cmd._typeIndex)) {
            case CommandType::BEGIN_RENDER_PASS: {
                // We may just wish to clear some state
                if (get<BeginRenderPassCommand>(cmd)->_descriptor._setViewport) {
                    hasWork = true;
                    break;
                }
            } break;
            case CommandType::CLEAR_RT:
            case CommandType::READ_BUFFER_DATA:
            case CommandType::COMPUTE_MIPMAPS:
            case CommandType::CLEAR_BUFFER_DATA:
            case CommandType::DISPATCH_COMPUTE:
            case CommandType::MEMORY_BARRIER:
            case CommandType::DRAW_TEXT:
            case CommandType::DRAW_COMMANDS:
            case CommandType::DRAW_IMGUI:
            case CommandType::BIND_DESCRIPTOR_SETS:
            case CommandType::BLIT_RT:
            case CommandType::RESET_RT:
            case CommandType::SEND_PUSH_CONSTANTS:
            case CommandType::BEGIN_PIXEL_BUFFER:
            case CommandType::SET_CAMERA:
            case CommandType::PUSH_CAMERA:
            case CommandType::POP_CAMERA:
            case CommandType::SET_CLIP_PLANES:
            case CommandType::SET_SCISSOR:
            case CommandType::SET_BLEND:
            case CommandType::SET_VIEWPORT:
            case CommandType::PUSH_VIEWPORT:
            case CommandType::POP_VIEWPORT:
            case CommandType::SET_CLIPING_STATE:
            case CommandType::SWITCH_WINDOW: {
                hasWork = true;
            } break;
            case CommandType::COPY_TEXTURE: {
                const CopyTextureCommand* crtCmd = get<CopyTextureCommand>(cmd);
                hasWork = crtCmd->_source._textureType != TextureType::COUNT && crtCmd->_destination._textureType != TextureType::COUNT;
            }break;
            case CommandType::CLEAR_TEXTURE: {
                const ClearTextureCommand* crtCmd = get<ClearTextureCommand>(cmd);
                hasWork = crtCmd->_texture != nullptr;
            }break;
            case CommandType::BIND_PIPELINE: {
                const BindPipelineCommand* crtCmd = get<BindPipelineCommand>(cmd);
                hasWork = crtCmd->_pipeline != nullptr && crtCmd->_pipeline->getHash() != 0;
            }break;
            default: break;
        };
    }

    if (!hasWork) {
        _commandOrder.resize(0);
        _commandCount.fill(U24(0u));
    }

    _batched = true;
}

void CommandBuffer::clean() {
    if (_commandOrder.empty()) {
        return;
    }

    OPTICK_EVENT();

    const Pipeline* prevPipeline = nullptr;
    const Rect<I32>* prevScissorRect = nullptr;
    const Rect<I32>* prevViewportRect = nullptr;
    const DescriptorSet* prevDescriptorSet = nullptr;
    const BlendingProperties* prevBlendProperties = nullptr;

    for (CommandEntry& cmd :_commandOrder) {
        bool erase = false;

        switch (static_cast<CommandType>(cmd._typeIndex)) {
            case CommandType::DRAW_COMMANDS :
            {
                OPTICK_EVENT("Clean Draw Commands");

                DrawCommand::CommandContainer& cmds = get<DrawCommand>(cmd)->_drawCommands;
                if (cmds.size() == 1) {
                    erase = cmds.begin()->_drawCount == 0u;
                } else {
                    erase = RemoveEmptyDrawCommands(cmds) && cmds.empty();
                }
            } break;
            case CommandType::BIND_PIPELINE : {
                OPTICK_EVENT("Clean Pipelines");

                const Pipeline* pipeline = get<BindPipelineCommand>(cmd)->_pipeline;
                // If the current pipeline is identical to the previous one, remove it
                if (prevPipeline == nullptr || prevPipeline->getHash() != pipeline->getHash()) {
                    prevPipeline = pipeline;
                } else {
                    erase = true;
                }
            }break;
            case CommandType::SEND_PUSH_CONSTANTS: {
                OPTICK_EVENT("Clean Push Constants");

                erase = get<SendPushConstantsCommand>(cmd)->_constants.empty();
            }break;
            case CommandType::BIND_DESCRIPTOR_SETS: {
                OPTICK_EVENT("Clean Descriptor Sets");

                const DescriptorSet& set = get<BindDescriptorSetsCommand>(cmd)->_set;
                if (prevDescriptorSet == nullptr || IsEmpty(set) || *prevDescriptorSet != set) {
                    prevDescriptorSet = &set;
                } else {
                    erase = true;
                }
            }break;
            case CommandType::DRAW_TEXT: {
                OPTICK_EVENT("Clean Draw Text");

                const TextElementBatch& textBatch = get<DrawTextCommand>(cmd)->_batch;

                erase = true;
                for (const TextElement& element : textBatch.data()) {
                    if (!element.text().empty()) {
                        erase = false;
                        break;
                    }
                }
            }break;
            case CommandType::SET_SCISSOR: {
                OPTICK_EVENT("Clean Scissor");

                const Rect<I32>& scissorRect = get<SetScissorCommand>(cmd)->_rect;
                if (prevScissorRect == nullptr || *prevScissorRect != scissorRect) {
                    prevScissorRect = &scissorRect;
                } else {
                    erase = true;
                }
            } break;
            case CommandType::SET_VIEWPORT: {
                OPTICK_EVENT("Clean Viewport");

                const Rect<I32>& viewportRect = get<SetViewportCommand>(cmd)->_viewport;

                if (prevViewportRect == nullptr || *prevViewportRect != viewportRect) {
                    prevViewportRect = &viewportRect;
                } else {
                    erase = true;
                }
            } break;
            case CommandType::SET_BLEND: {
                OPTICK_EVENT("Clean Viewport");

                const BlendingProperties& blendProperties = get<SetBlendCommand>(cmd)->_blendProperties;
                if (prevBlendProperties == nullptr || *prevBlendProperties != blendProperties) {
                    prevBlendProperties = &blendProperties;
                } else {
                    erase = true;
                }
            } break;
            default: break;
        };


        if (erase) {
            --_commandCount[cmd._typeIndex];
            cmd._data = PolyContainerEntry::INVALID_ENTRY_ID;
        } 
    }

    {
        OPTICK_EVENT("Remove redundant Pipelines");
        // Remove redundant pipeline changes
        auto* entry = eastl::next(begin(_commandOrder));
        for (; entry != cend(_commandOrder); ++entry) {
            const U8 typeIndex = entry->_typeIndex;

            if (static_cast<CommandType>(typeIndex) == CommandType::BIND_PIPELINE &&
                eastl::prev(entry)->_typeIndex == typeIndex)
            {
                --_commandCount[typeIndex];
                entry->_data = PolyContainerEntry::INVALID_ENTRY_ID;
            }
        }
    }
    {
        OPTICK_EVENT("Remove invalid draw commands");
        erase_if(_commandOrder, [](const CommandEntry& entry) noexcept { return entry._data == PolyContainerEntry::INVALID_ENTRY_ID; });
    }
}

// New use cases that emerge from production work should be checked here.
std::pair<ErrorType, size_t> CommandBuffer::validate() const {
    if_constexpr(Config::ENABLE_GPU_VALIDATION) {
        OPTICK_EVENT();

        size_t cmdIndex = 0u;
        bool pushedPass = false, pushedSubPass = false, pushedPixelBuffer = false;
        bool hasPipeline = false, hasDescriptorSets = false;
        I32 pushedDebugScope = 0, pushedCamera = 0, pushedViewport = 0;

        for (const CommandEntry& cmd : _commandOrder) {
            cmdIndex++;
            switch (static_cast<CommandType>(cmd._typeIndex)) {
                case CommandType::BEGIN_RENDER_PASS: {
                    if (pushedPass) {
                        return { ErrorType::MISSING_END_RENDER_PASS, cmdIndex };
                    }
                    pushedPass = true;
                } break;
                case CommandType::END_RENDER_PASS: {
                    if (!pushedPass) {
                        return { ErrorType::MISSING_BEGIN_RENDER_PASS, cmdIndex };
                    }
                    pushedPass = false;
                } break;
                case CommandType::BEGIN_RENDER_SUB_PASS: {
                    if (pushedSubPass) {
                        return { ErrorType::MISSING_END_RENDER_SUB_PASS, cmdIndex };
                    }
                    pushedSubPass = true;
                } break;
                case CommandType::END_RENDER_SUB_PASS: {
                    if (!pushedSubPass) {
                        return { ErrorType::MISSING_BEGIN_RENDER_SUB_PASS, cmdIndex };
                    }
                    pushedSubPass = false;
                } break;
                case CommandType::SET_BLEND_STATE: {
                    if (!pushedPass) {
                        return { ErrorType::MISSING_BEGIN_RENDER_PASS_FOR_BLEND, cmdIndex };
                    }
                }break;
                case CommandType::BEGIN_DEBUG_SCOPE: {
                    ++pushedDebugScope;
                } break;
                case CommandType::END_DEBUG_SCOPE: {
                    if (pushedDebugScope == 0) {
                        return { ErrorType::MISSING_PUSH_DEBUG_SCOPE, cmdIndex };
                    }
                    --pushedDebugScope;
                } break;
                case CommandType::BEGIN_PIXEL_BUFFER: {
                    if (pushedPixelBuffer) {
                        return { ErrorType::MISSING_END_PIXEL_BUFFER, cmdIndex };
                    }
                    pushedPixelBuffer = true;
                }break;
                case CommandType::END_PIXEL_BUFFER: {
                    if (!pushedPixelBuffer) {
                        return { ErrorType::MISSING_BEGIN_PIXEL_BUFFER, cmdIndex };
                    }
                    pushedPixelBuffer = false;
                }break;
                case CommandType::PUSH_CAMERA: {
                    ++pushedCamera;
                }break;
                case CommandType::POP_CAMERA: {
                    --pushedCamera;
                }break;
                case CommandType::PUSH_VIEWPORT: {
                    ++pushedViewport;
                }break;
                case CommandType::POP_VIEWPORT: {
                    --pushedViewport;
                }break;
                case CommandType::BIND_PIPELINE: {
                    hasPipeline = true;
                } break;
                case CommandType::DISPATCH_COMPUTE: 
                case CommandType::DRAW_TEXT:
                case CommandType::DRAW_IMGUI:
                case CommandType::DRAW_COMMANDS: {
                    if (!hasPipeline) {
                        return { ErrorType::MISSING_VALID_PIPELINE, cmdIndex };
                    }
                }break;
                case CommandType::BIND_DESCRIPTOR_SETS: {
                    hasDescriptorSets = true;
                }break;
                default: {
                    // no requirements yet
                }break;
            };
        }

        if (pushedPass) {
            return { ErrorType::MISSING_END_RENDER_PASS, cmdIndex };
        }
        if (pushedSubPass) {
            return { ErrorType::MISSING_END_RENDER_SUB_PASS, cmdIndex };
        }
        if (pushedPixelBuffer) {
            return { ErrorType::MISSING_END_PIXEL_BUFFER, cmdIndex };
        }
        if (pushedDebugScope != 0) {
            return { ErrorType::MISSING_POP_DEBUG_SCOPE, cmdIndex };
        }
        if (pushedCamera != 0) {
            return { ErrorType::MISSING_POP_CAMERA, cmdIndex };
        }
        if (pushedViewport != 0) {
            return { ErrorType::MISSING_POP_VIEWPORT, cmdIndex };
        }

        return { ErrorType::NONE, cmdIndex };

    } else/*_constexpr*/ { 
        return { ErrorType::NONE, 0u };
    }
}

void CommandBuffer::ToString(const CommandBase& cmd, const CommandType type, I32& crtIndent, string& out)
{
    const auto append = [](string& target, const string& text, const I32 indent) {
        for (I32 i = 0; i < indent; ++i) {
            target.append("    ");
        }
        target.append(text);
    };

    switch (type) {
        case CommandType::BEGIN_RENDER_PASS:
        case CommandType::BEGIN_PIXEL_BUFFER:
        case CommandType::BEGIN_RENDER_SUB_PASS: 
        case CommandType::BEGIN_DEBUG_SCOPE : {
            append(out, GFX::ToString(cmd, to_U16(crtIndent)), crtIndent);
            ++crtIndent;
        }break;
        case CommandType::END_RENDER_PASS:
        case CommandType::END_PIXEL_BUFFER:
        case CommandType::END_RENDER_SUB_PASS:
        case CommandType::END_DEBUG_SCOPE: {
            --crtIndent;
            append(out, GFX::ToString(cmd, to_U16(crtIndent)), crtIndent);
        }break;
        default: {
            append(out, GFX::ToString(cmd, to_U16(crtIndent)), crtIndent);
        }break;
    }
}

string CommandBuffer::toString() const {
    I32 crtIndent = 0;
    string out = "\n\n\n\n";
    size_t idx = 0u;
    for (const CommandEntry& cmd : _commandOrder) {
        out.append("[ " + std::to_string(idx++) +" ]: ");
        ToString(*get<CommandBase>(cmd), static_cast<CommandType>(cmd._typeIndex), crtIndent, out);
        out.append("\n");
    }
    out.append("\n\n\n\n");

    assert(crtIndent == 0);

    return out;
}

bool BatchDrawCommands(GenericDrawCommand& previousGDC, GenericDrawCommand& currentGDC) noexcept {
    // Instancing is not compatible with MDI. Well, it might be, but I can't be bothered a.t.m. to implement it -Ionut
    if (previousGDC._cmd.primCount != currentGDC._cmd.primCount && (previousGDC._cmd.primCount > 1 || currentGDC._cmd.primCount > 1)) {
        return false;
    }

    // Batch-able commands must share the same buffer and other various state
    if (!Compatible(previousGDC, currentGDC)) {
        return false;
    }
    const U32 offsetCrt = to_U32(currentGDC._commandOffset);
    const U32 offsetPrev = to_U32(previousGDC._commandOffset);
    if (offsetCrt - offsetPrev == previousGDC._drawCount) {
        // If the rendering commands are batch-able, increase the draw count for the previous one
        previousGDC._drawCount += currentGDC._drawCount;
        // And set the current command's draw count to zero so it gets removed from the list later on
        currentGDC._drawCount = 0;
        return true;
    }

    return false;
}

bool Merge(DrawCommand* prevCommand, DrawCommand* crtCommand) {
    OPTICK_EVENT();

    DrawCommand::CommandContainer& commands = prevCommand->_drawCommands;
    commands.insert(cend(commands),
                    eastl::make_move_iterator(begin(crtCommand->_drawCommands)),
                    eastl::make_move_iterator(end(crtCommand->_drawCommands)));
    crtCommand->_drawCommands.resize(0);

    {
        OPTICK_EVENT("Merge by offset");
        eastl::sort(begin(commands),
                    end(commands),
                    [](const GenericDrawCommand& a, const GenericDrawCommand& b) noexcept -> bool {
                        return a._commandOffset < b._commandOffset;
                    });

        do {
            const size_t commandCount = commands.size();
            for (size_t previousCommandIndex = 0, currentCommandIndex = 1;
                currentCommandIndex < commandCount;
                ++currentCommandIndex)
            {
                if (!BatchDrawCommands(commands[previousCommandIndex], commands[currentCommandIndex])) {
                    previousCommandIndex = currentCommandIndex;
                }
            }
        } while (RemoveEmptyDrawCommands(commands));
    }

    return true;
}

}; //namespace GFX
}; //namespace Divide
