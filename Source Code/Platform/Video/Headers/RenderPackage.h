/*
Copyright (c) 2018 DIVIDE-Studio
Copyright (c) 2009 Ionut Cava

This file is part of DIVIDE Framework.

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software
and associated documentation files (the "Software"), to deal in the Software
without restriction,
including without limitation the rights to use, copy, modify, merge, publish,
distribute, sublicense,
and/or sell copies of the Software, and to permit persons to whom the
Software is furnished to do so,
subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED,
INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
PARTICULAR PURPOSE AND NONINFRINGEMENT.
IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
DAMAGES OR OTHER LIABILITY,
WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR
IN CONNECTION WITH THE SOFTWARE
OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

*/

#pragma once
#ifndef _RENDER_PACKAGE_H_
#define _RENDER_PACKAGE_H_

#include "CommandBuffer.h"

namespace Divide {

class RenderingComponent;
class RenderPassExecutor;

namespace Attorney {
    class RenderPackageRenderPassExecutor;
    class RenderPackageRenderingComponent;
};

class RenderPackage {
    friend class Attorney::RenderPackageRenderPassExecutor;
    friend class Attorney::RenderPackageRenderingComponent;

    struct CommandData
    {
        static constexpr U32 INVALID_VALUE = U32_MAX;
        size_t _lodOffset = INVALID_VALUE;
        size_t _lodIdxCount = INVALID_VALUE;
        U32 _commandOffset = INVALID_VALUE;
        U32 _dataIdx = INVALID_VALUE;
    };
public:
    enum class MinQuality : U8 {
        FULL = 0,
        LOW,
        COUNT
    };

public:
    explicit RenderPackage() noexcept = default;
    ~RenderPackage();

    template<typename T>
    typename std::enable_if<std::is_base_of<GFX::CommandBase, T>::value, void>::type
    add(const T& command) { commands()->add(command); }

    template<typename T>
    [[nodiscard]] typename std::enable_if<std::is_base_of<GFX::CommandBase, T>::value, T*>::type
    get(const I32 index) const { return _commands->get<T>(index); }

    template<typename T>
    [[nodiscard]] typename std::enable_if<std::is_base_of<GFX::CommandBase, T>::value, size_t>::type
    count() const noexcept { return _commands->count<T>(); }

    [[nodiscard]] bool empty() const noexcept { return _commands == nullptr || _commands->empty(); }

    void setDrawOption(CmdRenderOptions option, bool state);
    void setDrawOptions(BaseType<CmdRenderOptions> optionMask, bool state);
    void enableOptions(BaseType<CmdRenderOptions> optionMask);
    void disableOptions(BaseType<CmdRenderOptions> optionMask);

    void clear();
    void setLoDIndexOffset(U8 lodIndex, size_t indexOffset, size_t indexCount) noexcept;
    void appendCommandBuffer(const GFX::CommandBuffer& commandBuffer);

    PROPERTY_RW(bool, textureDataDirty, true);
    PROPERTY_RW(MinQuality, qualityRequirement, MinQuality::FULL);
    PROPERTY_RW(size_t, sortKeyHashCache, 0u);

protected:
    [[nodiscard]] U32 updateAndRetrieveDrawCommands(U32 indirectBufferEntry, U32 startOffset, U8 lodLevel, DrawCommandContainer& cmdsInOut);
    [[nodiscard]] GFX::CommandBuffer* commands();
    void addDrawCommand(const GFX::DrawCommand& cmd);
    void addBindPipelineCommand(const GFX::BindPipelineCommand& cmd);
    void addPushConstantsCommand(const GFX::SendPushConstantsCommand& cmd);

    [[nodiscard]] bool setCommandDataIfDifferent(U32 startOffset, U32 dataIdx, size_t lodOffset, size_t lodCount) noexcept;

protected:
    CommandData _prevCommandData{};

    GFX::CommandBuffer* _commands = nullptr;
    std::array<std::pair<size_t, size_t>, 4> _lodIndexOffsets{};
    BaseType<CmdRenderOptions> _drawCommandOptions = to_base(CmdRenderOptions::RENDER_GEOMETRY);
    bool _isInstanced = false;
};

template <>
inline void RenderPackage::add<GFX::DrawCommand>(const GFX::DrawCommand& command) {
    addDrawCommand(command);
}

template <>
inline void RenderPackage::add<GFX::SendPushConstantsCommand>(const GFX::SendPushConstantsCommand& command) {
    addPushConstantsCommand(command);
}

template <>
inline void RenderPackage::add<GFX::BindPipelineCommand>(const GFX::BindPipelineCommand& command) {
    addBindPipelineCommand(command);
}

namespace Attorney {
    class RenderPackageRenderPassExecutor {
        static GFX::CommandBuffer* getCommandBuffer(RenderPackage* pkg) {
            return pkg->commands();
        }

        friend class RenderPassExecutor;
    };

    class RenderPackageRenderingComponent {
        static U32 updateAndRetrieveDrawCommands(RenderPackage& pkg, const U32 indirectBufferEntry, const U32 startOffset, const U8 lodLevel, DrawCommandContainer& cmdsInOut) {
            return pkg.updateAndRetrieveDrawCommands(indirectBufferEntry, startOffset, lodLevel, cmdsInOut);
        }

        friend class RenderingComponent;
    };
}; // namespace Attorney
}; // namespace Divide

#endif //_RENDER_PACKAGE_H_
