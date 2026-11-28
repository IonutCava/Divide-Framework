/*
Copyright (c) 2018-2026 DIVIDE-Studio
Copyright (c) 2009-2026 Ionut Cava

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
    INVALID_TASK_TYPE,
    INVALID_DESCRIPTOR_SET,
    INVALID_TEXTURE_HANDLE,
    INVALID_IMAGE_USAGE,
    INVALID_RENDER_PASS_FOR_PIPELINE,
    INVALID_SHADER_PROGRAM,
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
        "INVALID_TASK_TYPE",
        "INVALID_DESCRIPTOR_SET",
        "INVALID_TEXTURE_HANDLE",
        "INVALID_IMAGE_USAGE",
        "INVALID_RENDER_PASS_FOR_PIPELINE",
        "INVALID_SHADER_PROGRAM",
        "UNKNOW"
    };
};

static_assert(std::size( Names::errorType ) == to_base( ErrorType::COUNT ) + 1u, "ErrorType name array out of sync!");

/// Whether a draw can join a submission-time multi-draw batch. Checks run in declaration order,
/// so every rejected draw is attributed to exactly one reason.
enum class IndirectDrawCandidate : U8
{
    CANDIDATE = 0,
    OPTIONS,
    NO_INDICES,
    BUFFERS,
    MULTI_RECORD,
    INSTANCED,
    COUNT
};

/// Why a run of adjacent indirect draws stopped growing (see CollectIndirectDrawBatch).
enum class IndirectBatchStop : U8
{
    NOT_STARTED = 0,
    END_OF_BUFFER,
    NEXT_COMMANDS,
    NEXT_DRAW,
    BUFFERS,
    OFFSET_GAP,
    PUSH_DATA,
    DRAW_LIMIT,
    UNSUPPORTED,
    COUNT
};

namespace Names {
    static const char* indirectDrawCandidate[] = {
        "CANDIDATE",
        "OPTIONS",
        "NO_INDICES",
        "BUFFERS",
        "MULTI_RECORD",
        "INSTANCED",
        "UNKNOWN"
    };

    static const char* indirectBatchStop[] = {
        "NOT_STARTED",
        "END_OF_BUFFER",
        "NEXT_COMMANDS",
        "NEXT_DRAW",
        "BUFFERS",
        "OFFSET_GAP",
        "PUSH_DATA",
        "DRAW_LIMIT",
        "UNSUPPORTED",
        "UNKNOWN"
    };
};

static_assert(std::size( Names::indirectDrawCandidate ) == to_base( IndirectDrawCandidate::COUNT ) + 1u, "IndirectDrawCandidate name array out of sync!");
static_assert(std::size( Names::indirectBatchStop ) == to_base( IndirectBatchStop::COUNT ) + 1u, "IndirectBatchStop name array out of sync!");

enum class CommandType : U8;
class CommandBuffer;
struct CommandBase;
struct DrawCommand;
struct MemoryBarrierCommand;
struct SendPushConstantsCommand;

struct CommandBufferQueue
{
    static constexpr U32 COMMAND_BUFFER_INIT_SIZE = 4u;

    struct Entry
    {
        Handle<GFX::CommandBuffer> _buffer{INVALID_HANDLE<GFX::CommandBuffer>};
        bool _owning{false};
    };

    fixed_vector<Entry, COMMAND_BUFFER_INIT_SIZE, true> _commandBuffers;
};

void ResetCommandBufferQueue( CommandBufferQueue& queue );
void AddCommandBufferToQueue( CommandBufferQueue& queue, const Handle<GFX::CommandBuffer>& commandBuffer );
void AddCommandBufferToQueue( CommandBufferQueue& queue, Handle<GFX::CommandBuffer>&& commandBuffer );

class CommandBuffer : private NonCopyable
{
  public:
    static constexpr U32 COMMAND_BUFFER_INIT_SIZE = 32u;
    using CommandList = fixed_vector<CommandBase*, COMMAND_BUFFER_INIT_SIZE, true>;

  public:
    ~CommandBuffer();

    template<typename T> requires std::is_base_of_v<CommandBase, T>
    T* add();
    template<typename T> requires std::is_base_of_v<CommandBase, T>
    T* add(const T& command);
    template<typename T> requires std::is_base_of_v<CommandBase, T>
    T* add(T&& command);


    void add(const CommandBuffer& other);
    void add(const CommandBuffer& other, size_t first, size_t count);
    void add(Handle<CommandBuffer> other);
    /// Inserts a completed recording slot without changing the order of its surrounding commands.
    void insert(CommandBuffer&& other, size_t position);

    void batch();
    void clear();
    void clear( const char* name, size_t reservedCmdCount);

    /// Multi-line. indented list of all commands (and params for some of them)
    [[nodiscard]] string toString() const;

    /// Verify that the commands in the buffer are valid and in the right order 
    [[nodiscard]] std::tuple<ErrorType, size_t, std::string_view> validate() const;

    PROPERTY_R( CommandList, commands);

    [[nodiscard]] const Str<64>& name() const noexcept { return _name; }

  protected:

    void clean();
    bool cleanInternal();

  protected:
      bool _batched{ false };
      Str<64> _name;
};

void ToString(const CommandBase& cmd, CommandType type, I32& crtIndent, string& out);

// Return true if merge is successful
template<typename T = CommandBase> requires std::is_base_of_v<CommandBase, T>
[[nodiscard]] bool TryMergeCommands( CommandType type, T* prevCommand, T* crtCommand );
[[nodiscard]] bool Merge(DrawCommand* prevCommand, DrawCommand* crtCommand);
[[nodiscard]] bool Merge(MemoryBarrierCommand* lhs, MemoryBarrierCommand* rhs);
[[nodiscard]] bool Merge(SendPushConstantsCommand* lhs, SendPushConstantsCommand* rhs);
[[nodiscard]] bool BatchDrawCommands(GenericDrawCommand& previousGDC, GenericDrawCommand& currentGDC) noexcept;
[[nodiscard]] IndirectDrawCandidate ClassifyIndirectDraw( const GenericDrawCommand& draw ) noexcept;
/// Fuses the adjacent single-record indirect draw at drawIndex with the (push constants, draw) pairs that follow it.
/// Returns the number of commands consumed after drawIndex; stopOut, when provided, receives why the run ended.
[[nodiscard]] size_t CollectIndirectDrawBatch( const CommandBuffer::CommandList& commands,
                                               size_t drawIndex,
                                               bool shaderCompatible,
                                               U32 maxDrawCount,
                                               GenericDrawCommand& batchOut,
                                               IndirectBatchStop* stopOut = nullptr ) noexcept;

template<typename T> requires std::is_base_of_v<CommandBase, T>
FORCE_INLINE T* EnqueueCommand(CommandBuffer& buffer) { return buffer.add<T>(); }

template<typename T> requires std::is_base_of_v<CommandBase, T>
FORCE_INLINE T* EnqueueCommand(CommandBuffer& buffer, T& cmd) { return buffer.add(cmd); }

template<typename T> requires std::is_base_of_v<CommandBase, T>
FORCE_INLINE T* EnqueueCommand(CommandBuffer& buffer, T&& cmd) { return buffer.add( MOV(cmd) ); }

}; //namespace GFX
}; //namespace Divide

#endif //DVD_COMMAND_BUFFER_H_

#include "CommandBuffer.inl"
