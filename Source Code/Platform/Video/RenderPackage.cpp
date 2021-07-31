#include "stdafx.h"

#include "Headers/RenderPackage.h"

#include "Headers/CommandBufferPool.h"
#include "Headers/GFXDevice.h"

namespace Divide {

RenderPackage::~RenderPackage()
{
    DeallocateCommandBuffer(_commands);
}

void RenderPackage::clear() {
    if (_commands != nullptr) {
        _commands->clear();
        _prevCommandData = {};
    }
    textureDataDirty(true);
    _isInstanced = false;
}

void RenderPackage::setLoDIndexOffset(const U8 lodIndex, size_t indexOffset, size_t indexCount) noexcept {
    if (lodIndex < _lodIndexOffsets.size()) {
        _lodIndexOffsets[lodIndex] = {indexOffset, indexCount};
    }
}

void RenderPackage::addDrawCommand(const GFX::DrawCommand& cmd) {
    const bool wasInstanced = _isInstanced;

    if (!wasInstanced) {
        for (const GenericDrawCommand& drawCmd : cmd._drawCommands) {
            _isInstanced = drawCmd._cmd.primCount > 1;
            if (_isInstanced) {
                if (!_commands->exists<GFX::SendPushConstantsCommand>(0)) {
                    GFX::SendPushConstantsCommand constantsCmd = {};
                    constantsCmd._constants.set(_ID("DATA_INDICES"), GFX::PushConstantType::UINT, 0u);
                    add(constantsCmd);
                }
                break;
            }
        }
    }

    GFX::DrawCommand* newCmd = _commands->add(cmd);
    for (GenericDrawCommand& drawCmd : newCmd->_drawCommands) {
        Divide::enableOptions(drawCmd, _drawCommandOptions);
    }

    _prevCommandData = {};
}

void RenderPackage::addPushConstantsCommand(const GFX::SendPushConstantsCommand& cmd) {
    _commands->add(cmd);
    _prevCommandData = {};
}

void RenderPackage::setDrawOptions(const BaseType<CmdRenderOptions> optionMask, const bool state) {
    if (AllCompare(_drawCommandOptions, optionMask) == state) {
        return;
    }

    ToggleBit(_drawCommandOptions, optionMask, state);

    const auto& drawCommands = commands()->get<GFX::DrawCommand>();
    for (const auto& drawCommandEntry : drawCommands) {
        auto& drawCommand = static_cast<GFX::DrawCommand&>(*drawCommandEntry);
        for (GenericDrawCommand& drawCmd : drawCommand._drawCommands) {
            setOptions(drawCmd, optionMask, state);
        }
    }
}

void RenderPackage::setDrawOption(const CmdRenderOptions option, const bool state) {
    setDrawOptions(to_base(option), state);
}

void RenderPackage::enableOptions(const BaseType<CmdRenderOptions> optionMask) {
    setDrawOptions(optionMask, true);
}

void RenderPackage::disableOptions(const BaseType<CmdRenderOptions> optionMask) {
    setDrawOptions(optionMask, false);
}

void RenderPackage::appendCommandBuffer(const GFX::CommandBuffer& commandBuffer) {
    commands()->add(commandBuffer);
    _prevCommandData = {};
}

bool RenderPackage::setCommandDataIfDifferent(const U32 startOffset, const U32 dataIdx, const size_t lodOffset, const size_t lodCount) noexcept {
    if (_prevCommandData._dataIdx != dataIdx ||
        _prevCommandData._commandOffset != startOffset ||
        _prevCommandData._lodOffset != lodOffset ||
        _prevCommandData._lodIdxCount != lodCount)
    {
        _prevCommandData = { lodOffset, lodCount, startOffset, dataIdx };
        return true;
    }
    return false;
}

U32 RenderPackage::updateAndRetrieveDrawCommands(const NodeDataIdx dataIndex, U32 startOffset, const U8 lodLevel, DrawCommandContainer& cmdsInOut) {
    OPTICK_EVENT();

    const U32 dataIdx = ((dataIndex._transformIDX << 16) | dataIndex._materialIDX);
    const auto& [offset, count] = _lodIndexOffsets[std::min(lodLevel, to_U8(_lodIndexOffsets.size() - 1))];

    if (setCommandDataIfDifferent(startOffset, dataIdx, offset, count)) {
        if (_isInstanced) {
            for (GFX::CommandBase* cmd : commands()->get<GFX::SendPushConstantsCommand>()) {
                static_cast<GFX::SendPushConstantsCommand&>(*cmd)._constants.set(_ID("DATA_INDICES"), GFX::PushConstantType::UINT, dataIdx);
            }
        }

        const bool autoIndex = offset != 0u || count != 0u;

        const GFX::CommandBuffer::Container::EntryList& drawCommandEntries = commands()->get<GFX::DrawCommand>();
        for (GFX::CommandBase* const cmd : drawCommandEntries) {
            for (GenericDrawCommand& drawCmd : static_cast<GFX::DrawCommand&>(*cmd)._drawCommands) {
                drawCmd._commandOffset = startOffset++;
                drawCmd._cmd.baseInstance = _isInstanced ? 0u : dataIdx;
                drawCmd._cmd.firstIndex = autoIndex ? to_U32(offset) : drawCmd._cmd.firstIndex;
                drawCmd._cmd.indexCount = autoIndex ? to_U32(count) : drawCmd._cmd.indexCount;
            }
        }
    }

    U32 cmdCount = 0u;
    for (GFX::CommandBase* const cmd : commands()->get<GFX::DrawCommand>()) {
        for (const GenericDrawCommand& drawCmd : static_cast<GFX::DrawCommand&>(*cmd)._drawCommands) {
            cmdsInOut.push_back(drawCmd._cmd);
            ++cmdCount;
        }
    }

    return cmdCount;
}

GFX::CommandBuffer* RenderPackage::commands() {
    if (_commands == nullptr) {
        _commands = GFX::AllocateCommandBuffer();
    }

    return _commands;
}
}; //namespace Divide