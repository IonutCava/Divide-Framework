#include "stdafx.h"
#include "Headers/Commands.h"


#include "Headers/Pipeline.h"
#include "Platform/Video/Buffers/ShaderBuffer/Headers/ShaderBuffer.h"

namespace Divide {
namespace GFX {

IMPLEMENT_COMMAND(BindPipelineCommand);
IMPLEMENT_COMMAND(SendPushConstantsCommand);
IMPLEMENT_COMMAND(DrawCommand);
IMPLEMENT_COMMAND(SetViewportCommand);
IMPLEMENT_COMMAND(PushViewportCommand);
IMPLEMENT_COMMAND(PopViewportCommand);
IMPLEMENT_COMMAND(BeginRenderPassCommand);
IMPLEMENT_COMMAND(EndRenderPassCommand);
IMPLEMENT_COMMAND(BeginPixelBufferCommand);
IMPLEMENT_COMMAND(EndPixelBufferCommand);
IMPLEMENT_COMMAND(BeginRenderSubPassCommand);
IMPLEMENT_COMMAND(EndRenderSubPassCommand);
IMPLEMENT_COMMAND(BlitRenderTargetCommand);
IMPLEMENT_COMMAND(ClearRenderTargetCommand);
IMPLEMENT_COMMAND(ResetRenderTargetCommand);
IMPLEMENT_COMMAND(ResetAndClearRenderTargetCommand);
IMPLEMENT_COMMAND(CopyTextureCommand);
IMPLEMENT_COMMAND(ClearTextureCommand);
IMPLEMENT_COMMAND(ComputeMipMapsCommand);
IMPLEMENT_COMMAND(SetScissorCommand);
IMPLEMENT_COMMAND(SetCameraCommand);
IMPLEMENT_COMMAND(PushCameraCommand);
IMPLEMENT_COMMAND(PopCameraCommand);
IMPLEMENT_COMMAND(SetClipPlanesCommand);
IMPLEMENT_COMMAND(BindDescriptorSetsCommand);
IMPLEMENT_COMMAND(SetTexturesResidencyCommand);
IMPLEMENT_COMMAND(BeginDebugScopeCommand);
IMPLEMENT_COMMAND(EndDebugScopeCommand);
IMPLEMENT_COMMAND(AddDebugMessageCommand);
IMPLEMENT_COMMAND(DrawTextCommand);
IMPLEMENT_COMMAND(DrawIMGUICommand);
IMPLEMENT_COMMAND(DispatchComputeCommand);
IMPLEMENT_COMMAND(MemoryBarrierCommand);
IMPLEMENT_COMMAND(ReadBufferDataCommand);
IMPLEMENT_COMMAND(ClearBufferDataCommand);
IMPLEMENT_COMMAND(SetClippingStateCommand);
IMPLEMENT_COMMAND(ExternalCommand);

string ToString(const BindPipelineCommand& cmd, const U16 indent) {
    assert(cmd._pipeline != nullptr);

    const auto blendStateToString = [](const RTBlendState& state) -> string {
        if (!state._blendProperties.enabled()) {
            return "Disabled";
        }

        return Util::StringFormat("Colour {%d, %d, %d, %d}, Blend Src { %s }, Blend Dest { %s }, Blend Op { %s }, Blend Src Alpha { %s }, Blend Dest Alpha { %s }, Blend Op alpha { %s }",
                                  state._blendColour.r,
                                  state._blendColour.g,
                                  state._blendColour.b,
                                  state._blendColour.a,
                                  Divide::Names::blendProperty[to_base(state._blendProperties.blendSrc())],
                                  Divide::Names::blendProperty[to_base(state._blendProperties.blendDest())],
                                  Divide::Names::blendOperation[to_base(state._blendProperties.blendOp())],
                                  Divide::Names::blendProperty[to_base(state._blendProperties.blendSrcAlpha())],
                                  Divide::Names::blendProperty[to_base(state._blendProperties.blendDestAlpha())],
                                  Divide::Names::blendOperation[to_base(state._blendProperties.blendOpAlpha())]);
    };
    string ret = "\n";
    ret.append("    ");
    for (U16 j = 0; j < indent; ++j) {
        ret.append("    ");
    }
    ret.append(Util::StringFormat("Shader handle : %d\n", cmd._pipeline->descriptor()._shaderProgramHandle));
    ret.append("    ");
    for (U16 j = 0; j < indent; ++j) {
        ret.append("    ");
    }
    ret.append(Util::StringFormat("State hash : %zu\n", cmd._pipeline->hash()));
    ret.append("    ");
    for (U16 j = 0; j < indent; ++j) {
        ret.append("    ");
    }
    ret.append(Util::StringFormat("Primitive topology : %s\n", Divide::Names::primitiveType[to_base(cmd._pipeline->descriptor()._primitiveTopology)]));
    ret.append("    ");
    for (U16 j = 0; j < indent; ++j) {
        ret.append("    ");
    }
    ret.append("Blending states: \n");
    const RTBlendStates& blendStates = cmd._pipeline->descriptor()._blendStates;
    U8 idx = 0u;
    for (const RTBlendState& state : blendStates) {
        ret.append("    ");
        for (U16 j = 0; j < indent; ++j) {
            ret.append("    ");
        }
        ret.append(Util::StringFormat("%d: %s\n", idx++, blendStateToString(state)));
    }
    return ret;
}

string ToString(const SendPushConstantsCommand& cmd, const U16 indent) {
    string ret = "\n";

    for (const auto& it : cmd._constants.data()) {
        ret.append("    ");
        for (U16 j = 0; j < indent; ++j) {
            ret.append("    ");
        }
        ret.append(Util::StringFormat("Constant binding: %d Type: %d Data size: %zu\n", it.bindingHash(), to_base(it.type()), it.dataSize()));
    }

    return ret;
}

string ToString(const DrawCommand& cmd, const U16 indent)  {
    string ret = "\n";
    size_t i = 0;
    for (const GenericDrawCommand& drawCmd : cmd._drawCommands) {
        ret.append("    ");
        for (U16 j = 0; j < indent; ++j) {
            ret.append("    ");
        }
        ret.append(Util::StringFormat("%d: Draw count: %d Base instance: %d Instance count: %d Index count: %d\n", i++, drawCmd._drawCount, drawCmd._cmd.baseInstance, drawCmd._cmd.primCount, drawCmd._cmd.indexCount));
    }

    return ret;
}

string ToString(const SetViewportCommand& cmd, U16 indent) {
    return Util::StringFormat(" [%d, %d, %d, %d]", cmd._viewport.x, cmd._viewport.y, cmd._viewport.z, cmd._viewport.w);
}

string ToString(const PushViewportCommand& cmd, U16 indent) {
    return Util::StringFormat(" [%d, %d, %d, %d]", cmd._viewport.x, cmd._viewport.y, cmd._viewport.z, cmd._viewport.w);
}

string ToString(const BeginRenderPassCommand& cmd, U16 indent) {
    return " [ " + string(cmd._name.c_str()) + " ]";
}
string ToString(const SetScissorCommand& cmd, U16 indent) {
    return Util::StringFormat(" [%d, %d, %d, %d]", cmd._rect.x, cmd._rect.y, cmd._rect.z, cmd._rect.w);
}

string ToString(const SetClipPlanesCommand& cmd, const U16 indent) {
    string ret = "\n";

    auto& planes = cmd._clippingPlanes.planes();
    auto& states = cmd._clippingPlanes.planeState();
    for (U8 i = 0; i < to_U8(ClipPlaneIndex::COUNT); ++i) {
        if (states[i]) {
            ret.append("    ");
            for (U16 j = 0; j < indent; ++j) {
                ret.append("    ");
            }

            const vec4<F32>& eq = planes[i]._equation;

            ret.append(Util::StringFormat("Plane [%d] [ %5.2f %5.2f %5.2f - %5.2f ]\n", i, eq.x, eq.y, eq.z, eq.w));
        }
    }

    return ret;
}

string ToString(const SetCameraCommand& cmd, U16 indent) {
    string ret = "    ";
    ret.append(Util::StringFormat("[ Camera position (eye): [ %5.2f %5.2f %5.2f]\n", cmd._cameraSnapshot._eye.x, cmd._cameraSnapshot._eye.y, cmd._cameraSnapshot._eye.z));
    return ret;
}

string ToString(const BindDescriptorSetsCommand& cmd, const U16 indent) {
    string ret = Util::StringFormat(" [ Buffers: %d, Textures: %d ]\n", cmd._set._buffers.count(), cmd._set._textureData.count());

    for (U8 i = 0; i < cmd._set._buffers.count(); ++i) {
        const auto& it = cmd._set._buffers._entries[i];

        ret.append("    ");
        for (U16 j = 0; j < indent; ++j) {
            ret.append("    ");
        }
        
        ret.append(Util::StringFormat("Buffer [ %d - %d ] Range [%d - %d] ]\n", to_base(it._binding), it._buffer->getGUID(), it._elementRange.x, it._elementRange.y));
    }
    for (const TextureEntry& entry : cmd._set._textureData._entries) {
        if (entry._binding == INVALID_TEXTURE_BINDING) {
            continue;
        }

        ret.append("    ");
        for (U16 j = 0; j < indent; ++j) {
            ret.append("    ");
        }
        ret.append(Util::StringFormat("Texture [ %d - %d - %zu ]\n", entry._binding, entry._data._textureHandle, entry._sampler));
    }

    for (const auto& it : cmd._set._textureViews._entries) {
        ret.append("    ");
        for (U16 j = 0; j < indent; ++j) {
            ret.append("    ");
        }
        ret.append(Util::StringFormat("Texture layers [ %d - [%d - %d ]]\n", it._binding, it._view._layerRange.min, it._view._layerRange.max));
    }

    for (auto it : cmd._set._images._entries) {
        ret.append("    ");
        for (U16 j = 0; j < indent; ++j) {
            ret.append("    ");
        }
        ret.append(Util::StringFormat("Image binds: [ %d - [%d - %d - %s]", it._binding, it._layer, it._level, it._flag == Image::Flag::READ ? "READ" : it._flag == Image::Flag::WRITE ? "WRITE" : "READ_WRITE"));
    }
    return ret;
}

string ToString(const BeginDebugScopeCommand& cmd, const U16 indent) {
    return " [ " + string(cmd._scopeName.c_str()) + " ]";
}

string ToString(const AddDebugMessageCommand& cmd, const U16 indent) {
    string ret = "\n";
    for (U16 j = 0; j < indent; ++j) {
        ret.append("    ");
    }

    ret.append( " [ " + string(cmd._msg.c_str()) + " ]");
    return ret;
}

string ToString(const SetTexturesResidencyCommand& cmd, const U16 indent) {
    string ret = "\n";
    for (const SamplerAddress& address : cmd._addresses) {
        ret.append("    ");
        for (U16 j = 0; j < indent; ++j) {
            ret.append("    ");
        }
        ret.append(Util::StringFormat("Address: [ %zu ] State: [ %s ]\n", address, cmd._state ? "True" : "False"));
    }
    return ret;
}

string ToString(const DrawTextCommand& cmd, const U16 indent) {
    string ret = "\n";
    size_t i = 0;
    for (const TextElement& element : cmd._batch.data()) {
        ret.append("    ");
        for (U16 j = 0; j < indent; ++j) {
            ret.append("    ");
        }
        string string;
        for (const auto& it : element.text()) {
            string.append(it.c_str());
            string.append("\n");
        }
        ret.append(Util::StringFormat("%d: Text: [ %s ]", i++, string.c_str()));
    }
    return ret;
}

string ToString(const DispatchComputeCommand& cmd, U16 indent) {
    return Util::StringFormat(" [ Group sizes: %d %d %d]", cmd._computeGroupSize.x, cmd._computeGroupSize.y, cmd._computeGroupSize.z);
}

string ToString(const MemoryBarrierCommand& cmd, U16 indent) {
    return Util::StringFormat(" [ Mask: %d ]", cmd._barrierMask);
}

string ToString(const SetClippingStateCommand& cmd, U16 indent) {
    return Util::StringFormat(" [ Origin: %s ] [ Depth: %s ]", cmd._lowerLeftOrigin ? "LOWER_LEFT" : "UPPER_LEFT", cmd._negativeOneToOneDepth ? "-1 to 1 " : "0 to 1");
}

string ToString(const CommandBase& cmd, U16 indent) {
    string ret(indent, ' ');
    indent *= 2;
    ret.append(Names::commandType[to_base(cmd.Type())]);

    switch (cmd.Type()) {
        case CommandType::BIND_PIPELINE: {
            ret.append(ToString(static_cast<const BindPipelineCommand&>(cmd), indent));
        }break;
        case CommandType::SEND_PUSH_CONSTANTS:
        {
            ret.append(ToString(static_cast<const SendPushConstantsCommand&>(cmd), indent));
        }break;
        case CommandType::DRAW_COMMANDS:
        {
            ret.append(ToString(static_cast<const DrawCommand&>(cmd), indent));
        }break;
        case CommandType::SET_VIEWPORT:
        {
            ret.append(ToString(static_cast<const SetViewportCommand&>(cmd), indent));
        }break;
        case CommandType::PUSH_VIEWPORT:
        {
            ret.append(ToString(static_cast<const PushViewportCommand&>(cmd), indent));
        }break;
        case CommandType::BEGIN_RENDER_PASS:
        {
            ret.append(ToString(static_cast<const BeginRenderPassCommand&>(cmd), indent));
        }break;
        case CommandType::SET_SCISSOR:
        {
            ret.append(ToString(static_cast<const SetScissorCommand&>(cmd), indent));
        }break;
        case CommandType::SET_CLIP_PLANES:
        {
            ret.append(ToString(static_cast<const SetClipPlanesCommand&>(cmd), indent));
        }break;
        case CommandType::SET_CAMERA:
        {
            ret.append(ToString(static_cast<const SetCameraCommand&>(cmd), indent));
        }break;
        case CommandType::BIND_DESCRIPTOR_SETS:
        {
            ret.append(ToString(static_cast<const BindDescriptorSetsCommand&>(cmd), indent));
        }break;
        case CommandType::SET_TEXTURE_RESIDENCY:
        {
            ret.append(ToString(static_cast<const SetTexturesResidencyCommand&>(cmd), indent));
        }break;
        case CommandType::BEGIN_DEBUG_SCOPE:
        {
            ret.append(ToString(static_cast<const BeginDebugScopeCommand&>(cmd), indent));
        }break; 
        case CommandType::ADD_DEBUG_MESSAGE:
        {
            ret.append(ToString(static_cast<const AddDebugMessageCommand&>(cmd), indent));
        }break;
        case CommandType::DRAW_TEXT:
        {
            ret.append(ToString(static_cast<const DrawTextCommand&>(cmd), indent));
        }break;
        case CommandType::DISPATCH_COMPUTE:
        {
            ret.append(ToString(static_cast<const DispatchComputeCommand&>(cmd), indent));
        }break;
        case CommandType::MEMORY_BARRIER:
        {
            ret.append(ToString(static_cast<const MemoryBarrierCommand&>(cmd), indent));
        }break;
        case CommandType::SET_CLIPING_STATE:
        {
            ret.append(ToString(static_cast<const SetClippingStateCommand&>(cmd), indent));
        }break;
        default: break;
    }
    return ret;
}

}; //namespace GFX
}; //namespace Divide