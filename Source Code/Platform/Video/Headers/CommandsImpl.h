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
#ifndef _GFX_COMMAND_IMPL_H_
#define _GFX_COMMAND_IMPL_H_

#include "Commands.h"
#include "ClipPlanes.h"
#include "DescriptorSets.h"
#include "GenericDrawCommand.h"
#include "PushConstants.h"
#include "Platform/Video/Buffers/RenderTarget/Headers/RenderTarget.h"
#include "Rendering/Camera/Headers/CameraSnapshot.h"
#include "Utility/Headers/TextLabel.h"

struct ImDrawData;

namespace Divide {
class Pipeline;
class ShaderBuffer;
class PixelBuffer;

namespace GFX {

enum class CommandType : U8 {
    BEGIN_RENDER_PASS,
    END_RENDER_PASS,
    BEGIN_PIXEL_BUFFER,
    END_PIXEL_BUFFER,
    BEGIN_RENDER_SUB_PASS,
    END_RENDER_SUB_PASS,
    SET_BLEND_STATE,
    SET_VIEWPORT,
    PUSH_VIEWPORT,
    POP_VIEWPORT,
    SET_SCISSOR,
    SET_BLEND,
    CLEAR_RT,
    RESET_RT,
    RESET_AND_CLEAR_RT,
    BLIT_RT,
    COPY_TEXTURE,
    CLEAR_TEXTURE,
    COMPUTE_MIPMAPS,
    SET_CAMERA,
    PUSH_CAMERA,
    POP_CAMERA,
    SET_CLIP_PLANES,
    BIND_PIPELINE,
    BIND_DESCRIPTOR_SETS,
    SEND_PUSH_CONSTANTS,
    DRAW_COMMANDS,
    DRAW_TEXT,
    DRAW_IMGUI,
    DISPATCH_COMPUTE,
    MEMORY_BARRIER,
    READ_BUFFER_DATA,
    CLEAR_BUFFER_DATA,
    SET_TEXTURE_RESIDENCY,
    BEGIN_DEBUG_SCOPE,
    END_DEBUG_SCOPE,
    ADD_DEBUG_MESSAGE,
    SWITCH_WINDOW,
    SET_CLIPING_STATE,
    EXTERNAL,
    COUNT
};

namespace Names {
    static const char* commandType[] = {
        "BEGIN_RENDER_PASS", "END_RENDER_PASS", "BEGIN_PIXEL_BUFFER", "END_PIXEL_BUFFER", "BEGIN_RENDER_SUB_PASS",
        "END_RENDER_SUB_PASS", "SET_BLEND_STATE", "SET_VIEWPORT", "PUSH_VIEWPORT","POP_VIEWPORT", "SET_SCISSOR", "SET_BLEND", "CLEAR_RT",
        "RESET_RT", "RESET_AND_CLEAR_RT", "BLIT_RT", "COPY_TEXTURE", "CLEAR_TEXTURE", "COMPUTE_MIPMAPS", "SET_CAMERA",
        "PUSH_CAMERA", "POP_CAMERA", "SET_CLIP_PLANES", "BIND_PIPELINE", "BIND_DESCRIPTOR_SETS", "SEND_PUSH_CONSTANTS",
        "DRAW_COMMANDS", "DRAW_TEXT", "DRAW_IMGUI", "DISPATCH_COMPUTE", "MEMORY_BARRIER", "READ_BUFFER_DATA", "CLEAR_BUFFER_DATA",
        "SET_TEXTURE_RESIDENCY", "BEGIN_DEBUG_SCOPE","END_DEBUG_SCOPE", "ADD_DEBUG_MESSAGE", "SWITCH_WINDOW", "SET_CLIPING_STATE", "EXTERNAL", "UNKNOWN"
    };
};

static_assert(sizeof(Names::commandType) / sizeof(Names::commandType[0]) == to_size(CommandType::COUNT) + 1);

DEFINE_COMMAND_BEGIN(BindPipelineCommand, CommandType::BIND_PIPELINE);
    BindPipelineCommand(const Pipeline* pipeline) noexcept : _pipeline(pipeline) {}

    const Pipeline* _pipeline = nullptr;
DEFINE_COMMAND_END(BindPipelineCommand);

DEFINE_COMMAND_BEGIN(SendPushConstantsCommand, CommandType::SEND_PUSH_CONSTANTS);
    SendPushConstantsCommand(const PushConstants& constants) noexcept : _constants(constants) {}

    PushConstants _constants;
DEFINE_COMMAND_END(SendPushConstantsCommand);

DEFINE_COMMAND_BEGIN(DrawCommand, CommandType::DRAW_COMMANDS);
    using CommandContainer = eastl::fixed_vector<GenericDrawCommand, 4, true, eastl::dvd_allocator>;
    static_assert(sizeof(GenericDrawCommand) == 32, "Wrong command size! May cause performance issues. Disable assert to continue anyway.");

    DrawCommand(const GenericDrawCommand& cmd) : _drawCommands{ { cmd } } {}

    CommandContainer _drawCommands;
DEFINE_COMMAND_END(DrawCommand);

DEFINE_COMMAND_BEGIN(SetViewportCommand, CommandType::SET_VIEWPORT);
    SetViewportCommand(const Rect<I32>& viewport) noexcept : _viewport(viewport) {}

    Rect<I32> _viewport;
DEFINE_COMMAND_END(SetViewportCommand);

DEFINE_COMMAND_BEGIN(PushViewportCommand, CommandType::PUSH_VIEWPORT);
    PushViewportCommand(const Rect<I32>& viewport) noexcept : _viewport(viewport) {}

    Rect<I32> _viewport;
DEFINE_COMMAND_END(PushViewportCommand);

DEFINE_COMMAND(PopViewportCommand, CommandType::POP_VIEWPORT);

DEFINE_COMMAND_BEGIN(BeginRenderPassCommand, CommandType::BEGIN_RENDER_PASS);
    RenderTargetID _target;
    RTDrawDescriptor _descriptor;
    Str64 _name = "";
DEFINE_COMMAND_END(BeginRenderPassCommand);

DEFINE_COMMAND_BEGIN(EndRenderPassCommand, CommandType::END_RENDER_PASS);
    bool _setDefaultRTState = true;
DEFINE_COMMAND_END(EndRenderPassCommand);

DEFINE_COMMAND_BEGIN(BeginPixelBufferCommand, CommandType::BEGIN_PIXEL_BUFFER);
    PixelBuffer* _buffer = nullptr;
    DELEGATE<void, bufferPtr> _command;
DEFINE_COMMAND_END(BeginPixelBufferCommand);

DEFINE_COMMAND(EndPixelBufferCommand, CommandType::END_PIXEL_BUFFER);

DEFINE_COMMAND_BEGIN(BeginRenderSubPassCommand, CommandType::BEGIN_RENDER_SUB_PASS);
    U16 _mipWriteLevel = U16_MAX;
    vector<RenderTarget::DrawLayerParams> _writeLayers;
DEFINE_COMMAND_END(BeginRenderSubPassCommand);

DEFINE_COMMAND_BEGIN(SetBlendStateCommand, CommandType::SET_BLEND_STATE);
    RTBlendStates _blendStates;
DEFINE_COMMAND_END(SetBlendStateCommand);

DEFINE_COMMAND(EndRenderSubPassCommand, CommandType::END_RENDER_SUB_PASS);

DEFINE_COMMAND_BEGIN(BlitRenderTargetCommand, CommandType::BLIT_RT);
    // Depth layer to blit
    DepthBlitEntry _blitDepth;
    // List of colours + colour layer to blit
    std::array<ColourBlitEntry, RT_MAX_COLOUR_ATTACHMENTS> _blitColours;
    RenderTargetID _source;
    RenderTargetID _destination;
DEFINE_COMMAND_END(BlitRenderTargetCommand);

DEFINE_COMMAND_BEGIN(ClearRenderTargetCommand, CommandType::CLEAR_RT);
    ClearRenderTargetCommand(const RenderTargetID& target, const RTClearDescriptor& descriptor) noexcept : _target(target), _descriptor(descriptor) {}
    RenderTargetID _target;
    RTClearDescriptor _descriptor;
DEFINE_COMMAND_END(ClearRenderTargetCommand);

DEFINE_COMMAND_BEGIN(ResetRenderTargetCommand, CommandType::RESET_RT);
    RenderTargetID _source;
    RTDrawDescriptor _descriptor;
DEFINE_COMMAND_END(ResetRenderTargetCommand);

DEFINE_COMMAND_BEGIN(ResetAndClearRenderTargetCommand, CommandType::RESET_AND_CLEAR_RT);
    RenderTargetID _source;
    RTDrawDescriptor _drawDescriptor;
    RTClearDescriptor _clearDescriptor;
DEFINE_COMMAND_END(ResetAndClearRenderTargetCommand);

DEFINE_COMMAND_BEGIN(CopyTextureCommand, CommandType::COPY_TEXTURE);
    TextureData _source;
    TextureData _destination;
    CopyTexParams _params;
DEFINE_COMMAND_END(CopyTextureCommand);

DEFINE_COMMAND_BEGIN(ClearTextureCommand, CommandType::CLEAR_TEXTURE);
    Texture* _texture = nullptr;
    UColour4 _clearColour;
    vec2<I32> _depthRange;
    vec4<I32> _reactToClear;
    U8 _level = 0;
    bool _clearRect = false;
DEFINE_COMMAND_END(ClearTextureCommand);

DEFINE_COMMAND_BEGIN(ComputeMipMapsCommand, CommandType::COMPUTE_MIPMAPS);
    Texture* _texture = nullptr;
    vec2<U16> _layerRange = { 0u, 1u };
    vec2<U16> _mipRange = { 0u, 0u };
DEFINE_COMMAND_END(ComputeMipMapsCommand);

DEFINE_COMMAND_BEGIN(SetScissorCommand, CommandType::SET_SCISSOR);
    Rect<I32> _rect;
DEFINE_COMMAND_END(SetScissorCommand);

DEFINE_COMMAND_BEGIN(SetBlendCommand, CommandType::SET_BLEND);
    BlendingProperties _blendProperties;
DEFINE_COMMAND_END(SetBlendCommand);

DEFINE_COMMAND_BEGIN(SetCameraCommand, CommandType::SET_CAMERA);
    SetCameraCommand(const CameraSnapshot& cameraSnapshot) noexcept : _cameraSnapshot(cameraSnapshot) {}

    CameraSnapshot _cameraSnapshot;
DEFINE_COMMAND_END(SetCameraCommand);

DEFINE_COMMAND_BEGIN(PushCameraCommand, CommandType::PUSH_CAMERA);
    PushCameraCommand(const CameraSnapshot& cameraSnapshot) noexcept : _cameraSnapshot(cameraSnapshot) {}

    CameraSnapshot _cameraSnapshot;
DEFINE_COMMAND_END(PushCameraCommand);

DEFINE_COMMAND(PopCameraCommand, CommandType::POP_CAMERA);

DEFINE_COMMAND_BEGIN(SetClipPlanesCommand, CommandType::SET_CLIP_PLANES);
    SetClipPlanesCommand(const FrustumClipPlanes& clippingPlanes) noexcept : _clippingPlanes(clippingPlanes) {}

    FrustumClipPlanes _clippingPlanes;
DEFINE_COMMAND_END(SetClipPlanesCommand);

DEFINE_COMMAND_BEGIN(BindDescriptorSetsCommand, CommandType::BIND_DESCRIPTOR_SETS);
    BindDescriptorSetsCommand(const DescriptorSet& set) noexcept : _set(set) {}

    DescriptorSet _set;
DEFINE_COMMAND_END(BindDescriptorSetsCommand);

DEFINE_COMMAND_BEGIN(SetTexturesResidencyCommand, CommandType::SET_TEXTURE_RESIDENCY);
    std::array<SamplerAddress, 16> _addresses;
    bool _state = true;
DEFINE_COMMAND_END(SetTexturesResidencyCommand);

DEFINE_COMMAND_BEGIN(BeginDebugScopeCommand, CommandType::BEGIN_DEBUG_SCOPE);
    BeginDebugScopeCommand(const char* scopeName) noexcept : _scopeName(scopeName) {}

    Str64 _scopeName;
DEFINE_COMMAND_END(BeginDebugScopeCommand);

DEFINE_COMMAND(EndDebugScopeCommand, CommandType::END_DEBUG_SCOPE);

DEFINE_COMMAND_BEGIN(AddDebugMessageCommand, CommandType::ADD_DEBUG_MESSAGE);
    AddDebugMessageCommand(const char* msg) noexcept : _msg(msg) {}

    Str64 _msg;
DEFINE_COMMAND_END(AddDebugMessageCommand);

DEFINE_COMMAND_BEGIN(DrawTextCommand, CommandType::DRAW_TEXT);
    DrawTextCommand(TextElementBatch&& batch) noexcept : _batch(MOV(batch)) {}
    DrawTextCommand(TextElementBatch batch) noexcept : _batch(MOV(batch)) {}

    TextElementBatch _batch;
DEFINE_COMMAND_END(DrawTextCommand);

DEFINE_COMMAND_BEGIN(DrawIMGUICommand, CommandType::DRAW_IMGUI);
    ImDrawData* _data = nullptr;
    I64 _windowGUID = 0;
DEFINE_COMMAND_END(DrawIMGUICommand);

DEFINE_COMMAND_BEGIN(DispatchComputeCommand, CommandType::DISPATCH_COMPUTE);
    DispatchComputeCommand(const U32 xGroupSize, const U32 yGroupSize, const U32 zGroupSize) noexcept : _computeGroupSize(xGroupSize, yGroupSize, zGroupSize) {}
    DispatchComputeCommand(const vec3<U32>& groupSize) noexcept : _computeGroupSize(groupSize) {}

    vec3<U32> _computeGroupSize;
DEFINE_COMMAND_END(DispatchComputeCommand);

DEFINE_COMMAND_BEGIN(MemoryBarrierCommand, CommandType::MEMORY_BARRIER);
    MemoryBarrierCommand(const U32 mask) noexcept : _barrierMask(mask) {}

    U32 _barrierMask = 0u;
DEFINE_COMMAND_END(MemoryBarrierCommand);

DEFINE_COMMAND_BEGIN(ReadBufferDataCommand, CommandType::READ_BUFFER_DATA);
    ShaderBuffer* _buffer = nullptr;
    bufferPtr     _target = nullptr;
    U32           _offsetElementCount = 0;
    U32           _elementCount = 0;
DEFINE_COMMAND_END(ReadBufferDataCommand);

DEFINE_COMMAND_BEGIN(ClearBufferDataCommand, CommandType::CLEAR_BUFFER_DATA);
    ShaderBuffer* _buffer = nullptr;
    U32           _offsetElementCount = 0;
    U32           _elementCount = 0;
DEFINE_COMMAND_END(ClearBufferDataCommand);

DEFINE_COMMAND_BEGIN(SetClippingStateCommand, CommandType::SET_CLIPING_STATE)
    bool _lowerLeftOrigin = true;
    bool _negativeOneToOneDepth = true;
DEFINE_COMMAND_END(SetClippingStateCommand);

DEFINE_COMMAND_BEGIN(ExternalCommand, CommandType::EXTERNAL);
    DELEGATE<void> _cbk;
DEFINE_COMMAND_END(ExternalCommand);

}; //namespace GFX
}; //namespace Divide

#endif //_GFX_COMMAND_H_
