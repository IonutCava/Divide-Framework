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
    sortKeyHashCache(0u);
    _isInstanced = false;
}

void RenderPackage::setLoDIndexOffset(const U8 lodIndex, size_t indexOffset, size_t indexCount) noexcept {
    if (lodIndex < _lodIndexOffsets.size()) {
        _lodIndexOffsets[lodIndex] = {indexOffset, indexCount};
    }
}

void RenderPackage::addDrawCommand(const GFX::DrawCommand& cmd) {
    if (!_isInstanced) {
        for (const GenericDrawCommand& drawCmd : cmd._drawCommands) {
            if (drawCmd._cmd.primCount > 1) {
                _commands->get<GFX::SendPushConstantsCommand>(0)->_constants.set(_ID("INDIRECT_DATA_IDX"), GFX::PushConstantType::UINT, 0u);
                _isInstanced = true;
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

void RenderPackage::addBindPipelineCommand(const GFX::BindPipelineCommand& cmd) {
    if (empty() || count<GFX::BindPipelineCommand>() == 0) {
        sortKeyHashCache(cmd._pipeline->getHash());
    }

    commands()->add(cmd);
}

void RenderPackage::addPushConstantsCommand(const GFX::SendPushConstantsCommand& cmd) {
    commands()->add(cmd);
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

U32 RenderPackage::updateAndRetrieveDrawCommands(const U32 indirectBufferEntry, U32 startOffset, const U8 lodLevel, DrawCommandContainer& cmdsInOut) {
    OPTICK_EVENT();

    assert(indirectBufferEntry != U32_MAX);

    const auto& [offset, count] = _lodIndexOffsets[std::min(lodLevel, to_U8(_lodIndexOffsets.size() - 1))];

    const bool newDataIdx = _prevCommandData._dataIdx != indirectBufferEntry;
    if (setCommandDataIfDifferent(startOffset, indirectBufferEntry, offset, count)) {
        if (_isInstanced && newDataIdx) {
            for (GFX::CommandBase* cmd : commands()->get<GFX::SendPushConstantsCommand>()) {
                static_cast<GFX::SendPushConstantsCommand&>(*cmd)._constants.set(_ID("INDIRECT_DATA_IDX"), GFX::PushConstantType::UINT, indirectBufferEntry);
            }
        }

        const bool autoIndex = offset != 0u || count != 0u;

        const GFX::CommandBuffer::Container::EntryList& drawCommandEntries = commands()->get<GFX::DrawCommand>();
        for (GFX::CommandBase* const cmd : drawCommandEntries) {
            for (GenericDrawCommand& drawCmd : static_cast<GFX::DrawCommand&>(*cmd)._drawCommands) {
                drawCmd._commandOffset = startOffset++;
                drawCmd._cmd.baseInstance = _isInstanced ? 0u : (indirectBufferEntry + 1u); //Make sure to substract 1 in the shader!
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