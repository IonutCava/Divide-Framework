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

    const auto blendStateToString = [](const BlendingSettings& state) -> string {
        if (!state.enabled()) {
            return "Disabled";
        }

        return Util::StringFormat("Blend Src{% s}, Blend Dest{% s}, Blend Op{% s}, Blend Src Alpha{% s}, Blend Dest Alpha{% s}, Blend Op alpha{% s}",
                                  Divide::Names::blendProperty[to_base(state.blendSrc())],
                                  Divide::Names::blendProperty[to_base(state.blendDest())],
                                  Divide::Names::blendOperation[to_base(state.blendOp())],
                                  Divide::Names::blendProperty[to_base(state.blendSrcAlpha())],
                                  Divide::Names::blendProperty[to_base(state.blendDestAlpha())],
                                  Divide::Names::blendOperation[to_base(state.blendOpAlpha())]);
    };

    const auto attributeDescriptorToString = [](const U8 idx, const AttributeDescriptor& desc) -> string {
        if (desc._dataType == GFXDataFormat::COUNT) {
            return "Disabled";
        }


        return Util::StringFormat("Index { %d }, Binding { %d }, Components per element { %d }, Component format { %s }, Normalised { %s }, Stride in bytes { %zu }",
                                   idx,
                                   desc._bindingIndex,
                                   desc._componentsPerElement,
                                   Divide::Names::GFXDataFormat[to_base(desc._dataType)],
                                   desc._normalized ? "True" : "False",
                                   desc._strideInBytes);
    };

    string ret = "\n";
    ret.append("    ");
    for (U16 j = 0; j < indent; ++j) {
        ret.append("    ");
    }
    ret.append(Util::StringFormat("Shader handle : %d - %d\n", cmd._pipeline->descriptor()._shaderProgramHandle._id, cmd._pipeline->descriptor()._shaderProgramHandle._generation));
    ret.append("    ");
    for (U16 j = 0; j < indent; ++j) {
        ret.append("    ");
    }
    ret.append(Util::StringFormat("State hash : %zu\n", cmd._pipeline->hash()));
    ret.append("    ");
    for (U16 j = 0; j < indent; ++j) {
        ret.append("    ");
    }
    ShaderProgram* shader = ShaderProgram::FindShaderProgram(cmd._pipeline->descriptor()._shaderProgramHandle);
    if (shader) {
        ret.append(Util::StringFormat("Primitive topology : %s\n", Divide::Names::primitiveType[to_base(shader->descriptor()._primitiveTopology)]));
        ret.append("    ");
        for (U16 j = 0; j < indent; ++j) {
            ret.append("    ");
        }
    }
    {
        ret.append("Blending states: \n");
        const RTBlendStates& blendStates = cmd._pipeline->descriptor()._blendStates;

        ret.append(Util::StringFormat("Colour {%d, %d, %d, %d}", blendStates._blendColour.r, blendStates._blendColour.g, blendStates._blendColour.b, blendStates._blendColour.a));

        U8 idx = 0u;
        for (const BlendingSettings& state : blendStates._settings) {
            ret.append("    ");
            for (U16 j = 0; j < indent; ++j) {
                ret.append("    ");
            }
            ret.append(Util::StringFormat("%d: %s\n", idx++, blendStateToString(state)));
        }
    }
    if (shader) {
        ret.append("Vertex format: \n");
        U8 idx = 0u;
        for (const AttributeDescriptor& desc : shader->descriptor()._vertexFormat) {
            ret.append("    ");
            for (U16 j = 0; j < indent; ++j) {
                ret.append("    ");
            }
            ret.append(Util::StringFormat("%d: %s\n", idx, attributeDescriptorToString(idx, desc)));
            ++idx;
        }
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
    U8 bufferCount = 0u;
    U8 imageCount = 0u;
    const DescriptorSet& set = cmd._set;
    for (const auto& binding : set._bindings) {
        if (binding._type == DescriptorSetBindingType::ATOMIC_BUFFER ||
            binding._type == DescriptorSetBindingType::SHADER_STORAGE_BUFFER ||
            binding._type == DescriptorSetBindingType::UNIFORM_BUFFER) {
            ++bufferCount;
        } else if (binding._type == DescriptorSetBindingType::COMBINED_IMAGE_SAMPLER ||
                   binding._type == DescriptorSetBindingType::IMAGE) {
            ++imageCount;
        }
    }
    string ret = Util::StringFormat(" [ Buffers: %d, Images: %d ]\n", bufferCount, imageCount);

    for (const auto& binding : set._bindings) {
        if (binding._type == DescriptorSetBindingType::ATOMIC_BUFFER ||
            binding._type == DescriptorSetBindingType::SHADER_STORAGE_BUFFER ||
            binding._type == DescriptorSetBindingType::UNIFORM_BUFFER)
        {
            ret.append("    ");
            for (U16 j = 0; j < indent; ++j) {
                ret.append("    ");
            }
            const auto& data = binding._data;
            const auto& bufferEntry = data.As<ShaderBufferEntry>();
            ret.append(Util::StringFormat("Stage mask [ %d ] Buffer [ %d - %d ] Range [%zu - %zu] ]\n",
                       to_U32(binding._shaderStageVisibility),
                       binding._resourceSlot,
                       bufferEntry._buffer->getGUID(),
                       bufferEntry._range._startOffset,
                       bufferEntry._range._length));
        }
    }
    for (const auto& binding : set._bindings) {
        if (binding._type == DescriptorSetBindingType::COMBINED_IMAGE_SAMPLER ||
            binding._type == DescriptorSetBindingType::IMAGE_VIEW ||
            binding._type == DescriptorSetBindingType::IMAGE)
        {
            if (binding._resourceSlot == INVALID_TEXTURE_BINDING) {
                continue;
            }
            
            ret.append("    ");
            for (U16 j = 0; j < indent; ++j) {
                ret.append("    ");
            }
            if (binding._type == DescriptorSetBindingType::COMBINED_IMAGE_SAMPLER) {
                ret.append(Util::StringFormat("Stage mask [ %d ] Texture [ %d - %d - %zu ]\n",
                           to_U32(binding._shaderStageVisibility),
                           binding._resourceSlot,
                           binding._data.As<DescriptorCombinedImageSampler>()._image._textureHandle,
                           binding._data.As<DescriptorCombinedImageSampler>()._samplerHash));
            } else if (binding._type == DescriptorSetBindingType::IMAGE_VIEW) {
                ret.append(Util::StringFormat("Stage mask [ %d ] Texture layers [ %d - [%d - %d ]]\n",
                           to_U32(binding._shaderStageVisibility),
                           binding._resourceSlot,
                           binding._data.As<ImageViewEntry>()._view._layerRange.min,
                           binding._data.As<ImageViewEntry>()._view._layerRange.max));
            } else {
                ret.append(Util::StringFormat("Stage mask [ %d ] Image binds: [ %d - [%d - %d - %s]",
                           to_U32(binding._shaderStageVisibility),
                           binding._resourceSlot,
                           binding._data.As<Image>()._layer,
                           binding._data.As<Image>()._level,
                           binding._data.As<Image>()._flag == Image::Flag::READ
                                                            ? "READ" 
                                                            : binding._data.As<Image>()._flag == Image::Flag::WRITE
                                                                                               ? "WRITE" : "READ_WRITE"));
            }
        }
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
    string ret = Util::StringFormat(" [ Mask: %d ] [ Buffer locks: %zu ]", cmd._barrierMask, cmd._bufferLocks.size());
 
    for (auto it : cmd._bufferLocks) {
        ret.append("    ");
        for (U16 j = 0; j < indent; ++j) {
            ret.append("    ");
        }
        ret.append(Util::StringFormat("Buffer lock: [ %d - [%zu - %zu] ]", it._targetBuffer->getGUID(), it._range._startOffset, it._range._length));
    }

    return ret;
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