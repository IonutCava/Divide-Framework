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

namespace Divide {
struct GenericDrawCommand;

namespace GFX {

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

struct CommandBufferQueue
{
    static constexpr U32 COMMAND_BUFFER_INIT_SIZE = 4u;

    struct Entry
    {
        Handle<GFX::CommandBuffer> _buffer{INVALID_HANDLE<GFX::CommandBuffer>};
        bool _owning{false};
    };

    eastl::fixed_vector<Entry, COMMAND_BUFFER_INIT_SIZE, true, eastl::dvd_allocator> _commandBuffers;
};

void ResetCommandBufferQueue( CommandBufferQueue& queue );
void AddCommandBufferToQueue( CommandBufferQueue& queue, const Handle<GFX::CommandBuffer>& commandBuffer );
void AddCommandBufferToQueue( CommandBufferQueue& queue, Handle<GFX::CommandBuffer>&& commandBuffer );

#pragma pack(push, 1)
struct CommandEntry
{
    static constexpr U32 INVALID_ENTRY_ID = U32_MAX;

    CommandEntry() noexcept = default;

    explicit CommandEntry( const CommandType type, const U32 element ) noexcept;

    struct IDX
    {
        U32 _element : 24;
        U32 _type : 8;
    };

    union
    {
        IDX _idx;
        U32 _data{ INVALID_ENTRY_ID };
    };
};
#pragma pack(pop)

class CommandBuffer : private NonCopyable
{  
  public:

      using CommandOrderContainer = eastl::fixed_vector<CommandEntry, 128, true, eastl::dvd_allocator>;
      using CommandList = vector_fast<CommandBase*>;

  public:

    template<typename T> requires std::is_base_of_v<CommandBase, T>
    T* add();
    template<typename T> requires std::is_base_of_v<CommandBase, T>
    T* add(const T& command);
    template<typename T> requires std::is_base_of_v<CommandBase, T>
    T* add(T&& command);


    void add(const CommandBuffer& other);
    void add(Handle<CommandBuffer> other);

    void batch();
    void clear(bool clearMemory = true);

    template<typename T = CommandBase> requires std::is_base_of_v<CommandBase, T>
    [[nodiscard]] T* get(const CommandEntry& commandEntry) const;

    /// Multi-line. indented list of all commands (and params for some of them)
    [[nodiscard]] string toString() const;

    /// Verify that the commands in the buffer are valid and in the right order 
    [[nodiscard]] std::pair<ErrorType, size_t> validate() const;

    PROPERTY_R(CommandOrderContainer, commandOrder);

  protected:
    template<CommandType EnumVal>
    friend struct Command;

    template <typename T, size_t BlockSize>
    friend class MemoryPool;

    CommandBuffer();
    ~CommandBuffer();

    [[nodiscard]] CommandEntry addCommandEntry( CommandType type );

    void clean();
    bool cleanInternal();
    

    static void ToString(const CommandBase& cmd, CommandType type, I32& crtIndent, string& out);

  protected:
      eastl::array<U32, to_base(CommandType::COUNT)> _commandCount{};
      eastl::array<CommandList, to_base( CommandType::COUNT )> _collection{};
      bool _batched{ false };
};

// Return true if merge is successful
template<typename T = CommandBase> requires std::is_base_of_v<CommandBase, T>
[[nodiscard]] bool TryMergeCommands( CommandType type, T* prevCommand, T* crtCommand );
[[nodiscard]] bool Merge(DrawCommand* prevCommand, DrawCommand* crtCommand);
[[nodiscard]] bool Merge(MemoryBarrierCommand* lhs, MemoryBarrierCommand* rhs);
[[nodiscard]] bool BatchDrawCommands(GenericDrawCommand& previousGDC, GenericDrawCommand& currentGDC) noexcept;

template<typename T> requires std::is_base_of_v<CommandBase, T>
FORCE_INLINE T* EnqueueCommand(CommandBuffer& buffer) { return buffer.add<T>(); }

template<typename T> requires std::is_base_of_v<CommandBase, T>
FORCE_INLINE T* EnqueueCommand(CommandBuffer& buffer, T& cmd) { return buffer.add(cmd); }

template<typename T> requires std::is_base_of_v<CommandBase, T>
FORCE_INLINE T* EnqueueCommand(CommandBuffer& buffer, T&& cmd) { return buffer.add( MOV(cmd) ); }

template<typename T> requires std::is_base_of_v<CommandBase, T>
FORCE_INLINE T* EnqueueCommand(Handle<CommandBuffer> buffer) { return buffer._ptr->add<T>(); }

template<typename T> requires std::is_base_of_v<CommandBase, T>
FORCE_INLINE T* EnqueueCommand(Handle<CommandBuffer> buffer, T& cmd) { return buffer._ptr->add(cmd); }

template<typename T> requires std::is_base_of_v<CommandBase, T>
FORCE_INLINE T* EnqueueCommand(Handle<CommandBuffer> buffer, T&& cmd) { return buffer._ptr->add( MOV(cmd) ); }

}; //namespace GFX
}; //namespace Divide

#endif //DVD_COMMAND_BUFFER_H_

#include "CommandBuffer.inl"
