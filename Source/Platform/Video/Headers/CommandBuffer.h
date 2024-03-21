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
#ifndef DVD_COMMAND_BUFFER_H_
#define DVD_COMMAND_BUFFER_H_

#include "CommandsImpl.h"
#include "Core/TemplateLibraries/Headers/PolyContainer.h"

namespace Divide {
struct GenericDrawCommand;

namespace GFX {

void DELETE_CMD(CommandBase*& cmd);
[[nodiscard]] size_t RESERVE_CMD(U8 typeIndex) noexcept;

enum class ErrorType : U8
{
    NONE = 0,
    MISSING_BEGIN_RENDER_PASS,
    INVALID_BEGIN_RENDER_PASS,
    MISSING_END_RENDER_PASS,
    MISSING_BEGIN_GPU_QUERY,
    MISSING_END_GPU_QUERY,
    MISSING_PUSH_DEBUG_SCOPE,
    MISSING_POP_DEBUG_SCOPE,
    MISSING_POP_CAMERA,
    MISSING_POP_VIEWPORT,
    MISSING_VALID_PIPELINE,
    MISSING_BLIT_DESCRIPTOR_SET,
    INVALID_DISPATCH_COUNT,
    INVALID_DESCRIPTOR_SET,
    INVALID_RENDER_PASS_FOR_PIPELINE,
    COUNT
};

namespace Names {
    static const char* errorType[] = {
        "NONE",
        "MISSING_BEGIN_RENDER_PASS",
        "INVALID_BEGIN_RENDER_PASS",
        "MISSING_END_RENDER_PASS",
        "MISSING_BEGIN_GPU_QUERY",
        "MISSING_END_GPU_QUERY",
        "MISSING_PUSH_DEBUG_SCOPE", 
        "MISSING_POP_DEBUG_SCOPE",
        "MISSING_POP_CAMERA",
        "MISSING_POP_VIEWPORT", 
        "MISSING_VALID_PIPELINE",
        "MISSING_BLIT_DESCRIPTOR_SET",
        "INVALID_DISPATCH_COUNT",
        "INVALID_DESCRIPTOR_SET",
        "INVALID_RENDER_PASS_FOR_PIPELINE",
        "UNKNOW"
    };
};

static_assert(std::size( Names::errorType ) == to_base( ErrorType::COUNT ) + 1u, "ErrorType name array out of sync!");

class CommandBuffer : private NonCopyable
{
    friend class CommandBufferPool;
  public:
      using CommandEntry = PolyContainerEntry;
      using Container = PolyContainer<CommandBase, to_base(CommandType::COUNT), DELETE_CMD, RESERVE_CMD>;
      using CommandOrderContainer = eastl::fixed_vector<CommandEntry, 512, true, eastl::dvd_allocator>;

  public:
    CommandBuffer() = default;
    ~CommandBuffer() = default;

    template<typename T> requires std::is_base_of_v<CommandBase, T>
    T* add();
    template<typename T> requires std::is_base_of_v<CommandBase, T>
    T* add(const T& command);
    template<typename T> requires std::is_base_of_v<CommandBase, T>
    T* add(T&& command);

    [[nodiscard]] std::pair<ErrorType, size_t> validate() const;

    void add(const CommandBuffer& other);

    void clean();
    void batch();

    // Return true if merge is successful
    template<typename T> requires std::is_base_of_v<CommandBase, T>
    [[nodiscard]] bool tryMergeCommands(CommandType type, T* prevCommand, T* crtCommand) const;

    [[nodiscard]] bool exists(const CommandEntry& commandEntry) const noexcept;

    template<typename T> requires std::is_base_of_v<CommandBase, T>
    [[nodiscard]] const Container::EntryList& get() const;

    template<typename T> requires std::is_base_of_v<CommandBase, T>
    [[nodiscard]] T* get(const CommandEntry& commandEntry) const;

    template<typename T> requires std::is_base_of_v<CommandBase, T>
    [[nodiscard]] T* get(const CommandEntry& commandEntry);

    template<typename T> requires std::is_base_of_v<CommandBase, T>
    [[nodiscard]] T* get(U24 index);

    template<typename T> requires std::is_base_of_v<CommandBase, T>
    [[nodiscard]] T* get(U24 index) const;

    [[nodiscard]] bool exists(U8 typeIndex, U24 index) const noexcept;

    template<typename T> requires std::is_base_of_v<CommandBase, T>
    [[nodiscard]] bool exists(U24 index) const noexcept;

    inline CommandOrderContainer& operator()() noexcept;
    inline const CommandOrderContainer& operator()() const noexcept;

    inline void clear(bool clearMemory = true);
    [[nodiscard]] inline bool empty() const noexcept;

    // Multi-line. indented list of all commands (and params for some of them)
    [[nodiscard]] string toString() const;

    template<typename T> requires std::is_base_of_v<CommandBase, T>
    [[nodiscard]] size_t count() const noexcept;

    [[nodiscard]] inline size_t size() const noexcept { return _commandOrder.size(); }

  protected:
    template<typename T, CommandType enumVal>
    friend struct Command;

    template<typename T> requires std::is_base_of_v<CommandBase, T>
    [[nodiscard]] T* allocateCommand();

    static void ToString(const CommandBase& cmd, CommandType type, I32& crtIndent, string& out);

  protected:
      CommandOrderContainer _commandOrder{};
      eastl::array<U24, to_base(CommandType::COUNT)> _commandCount{};

      Container _commands{};
      bool _batched{ false };
};

[[nodiscard]] bool Merge(DrawCommand* prevCommand, DrawCommand* crtCommand);
[[nodiscard]] bool Merge(MemoryBarrierCommand* lhs, MemoryBarrierCommand* rhs);
[[nodiscard]] bool BatchDrawCommands(GenericDrawCommand& previousGDC, GenericDrawCommand& currentGDC) noexcept;

template<typename T> requires std::is_base_of_v<CommandBase, T>
FORCE_INLINE T* EnqueueCommand(CommandBuffer& buffer) { return buffer.add<T>(); }

template<typename T> requires std::is_base_of_v<CommandBase, T>
FORCE_INLINE T* EnqueueCommand(CommandBuffer& buffer, T& cmd) { return buffer.add(cmd); }

template<typename T> requires std::is_base_of_v<CommandBase, T>
FORCE_INLINE T* EnqueueCommand(CommandBuffer& buffer, T&& cmd) { return buffer.add(cmd); }

}; //namespace GFX
}; //namespace Divide

#endif //DVD_COMMAND_BUFFER_H_

#include "CommandBuffer.inl"
