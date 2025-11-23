

#include "Headers/Commands.h"
#include "Headers/Pipeline.h"

#include "Core/Resources/Headers/ResourceCache.h"

#include "Platform/Video/Textures/Headers/Texture.h"
#include "Platform/Video/Shaders/Headers/ShaderProgram.h"
#include "Platform/Video/Buffers/VertexBuffer/Headers/GPUBuffer.h"
#include "Platform/Video/Buffers/ShaderBuffer/Headers/ShaderBuffer.h"

#include <bitset>

namespace Divide {
namespace GFX {

static string ToString(const BindPipelineCommand& cmd, U16 indent)
{
    assert(cmd._pipeline != nullptr);

    const auto blendStateToString = [](const BlendingSettings& state) -> string {
        if (!state.enabled()) {
            return "Disabled";
        }

        return Util::StringFormat("Blend Src[ {} ], Blend Dest[ {} ], Blend Op[ {} ], Blend Src Alpha[ {} ], Blend Dest Alpha[ {} ], Blend Op alpha[ {} ]",
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


        return Util::StringFormat("Index [ {} ], Binding [ {} ], Components per element [ {} ], Component format [ {} ], Normalised [ {} ], Stride in bytes [ {} ]",
                                   idx,
                                   desc._vertexBindingIndex,
                                   desc._componentsPerElement,
                                   Divide::Names::GFXDataFormat[to_base(desc._dataType)],
                                   desc._normalized ? "True" : "False",
                                   desc._strideInBytes);
    };

    const auto vertexFormatToString = []( const U8 idx, const VertexBinding& binding ) -> string
    {
        return Util::StringFormat("Index [ {} ] Binding [ {} ], Stride in bytes [ {} ], Per Vertex Input Rate [ {} ]",
                                  idx,
                                  binding._bufferBindIndex,
                                  binding._strideInBytes,
                                  binding._perVertexInputRate ? "True" : "False");
    };

    string ret = "\n";
    for (U16 j = 0; j < indent; ++j) {
        ret.append("    ");
    }
    ret.append(Util::StringFormat("Shader handle : {} - {}\n", cmd._pipeline->descriptor()._shaderProgramHandle._index, cmd._pipeline->descriptor()._shaderProgramHandle._generation));
    ret.append("    ");
    for (U16 j = 0; j < indent; ++j) {
        ret.append("    ");
    }
    ret.append(Util::StringFormat("State hash : {}\n", cmd._pipeline->stateHash()));
    ret.append("    ");
    for (U16 j = 0; j < indent; ++j) {
        ret.append("    ");
    }
    ShaderProgram* shader = Get(cmd._pipeline->descriptor()._shaderProgramHandle);
    if (shader) {
        ret.append(Util::StringFormat("Primitive topology : {}\n", Divide::Names::primitiveType[to_base(cmd._pipeline->descriptor()._primitiveTopology)]));
        ret.append("    ");
        for (U16 j = 0; j < indent; ++j) {
            ret.append("    ");
        }
    }
    {
        ret.append("Blending states: \n");
        indent += 1u;
        const RTBlendStates& blendStates = cmd._pipeline->descriptor()._blendStates;
        ret.append("    ");
        for (U16 j = 0; j < indent; ++j) {
            ret.append("    ");
        }
        ret.append(Util::StringFormat("Colour [ {}, {}, {}, {} ]\n", blendStates._blendColour.r, blendStates._blendColour.g, blendStates._blendColour.b, blendStates._blendColour.a));

        U8 idx = 0u;
        for (const BlendingSettings& state : blendStates._settings) {
            ret.append("    ");
            for (U16 j = 0; j < indent; ++j) {
                ret.append("    ");
            }
            ret.append(Util::StringFormat("{}: {}\n", idx++, blendStateToString(state)));
        }
        indent -= 1u;
    }
    if (shader) {
        ret.append("    ");
        for (U16 j = 0; j < indent; ++j) {
            ret.append("    ");
        }
        ret.append("Vertex format: \n");

        indent += 1u;
        U8 idx = 0u;
        for (const AttributeDescriptor& desc : cmd._pipeline->descriptor()._vertexFormat._attributes) {
            ret.append("    ");
            for (U16 j = 0; j < indent; ++j) {
                ret.append("    ");
            }
            ret.append(Util::StringFormat("{}: {}\n", idx, attributeDescriptorToString(idx, desc)));
            ++idx;
        }
        idx = 0u;
        for ( const VertexBinding& binding : cmd._pipeline->descriptor()._vertexFormat._vertexBindings )
        {
            ret.append( "    " );
            for ( U16 j = 0; j < indent; ++j )
            {
                ret.append( "    " );
            }
            ret.append( Util::StringFormat( "{}: {}\n", idx, vertexFormatToString( idx, binding ) ) );
            ++idx;
        }
        indent -= 1u;
    }
    return ret;
}

static string ToString(const SendPushConstantsCommand& cmd, U16 indent)
{
    string ret = "\n";

    if ( cmd._uniformData != nullptr )
    {
        for (const auto& it : cmd._uniformData->entries())
        {
            ret.append("    ");
            for (U16 j = 0; j < indent; ++j) {
                ret.append("    ");
            }
            ret.append(Util::StringFormat("Constant binding: {} Type: {} Data range: [ {} - {} ]\n", it._bindingHash, to_base(it._type), it._range._startOffset, it._range._startOffset + it._range._length));
        }
    }
    ret.append("    ");
    for (U16 j = 0; j < indent; ++j) {
        ret.append("    ");
    }
    ret.append(cmd._fastData.set() ? "Has push constants specified: \n" : "No push constant data specified");

    if ( cmd._fastData.set() )
    {
        for (U8 d = 0u; d < 2u; ++d ) {
            ret.append( "    " );
            for ( U16 j = 0; j < indent; ++j )
            {
                ret.append( "    " );
            }

            const mat4<F32>& datad = cmd._fastData.data[d];
            ret.append(Util::StringFormat("Data {}:\n", d));

            indent += 1u;
            for ( U8 r = 0u; r < 4; ++r )
            {
                ret.append( "    " );
                for ( U16 j = 0; j < indent; ++j )
                {
                    ret.append( "    " );
                }

                ret.append(Util::StringFormat(" {:.2f} {:.2f} {:.2f} {:.2f}\n", datad.m[r][0], datad.m[r][1], datad.m[r][2] , datad.m[r][3]));
            }
            indent -= 1u;
        }
    }

    return ret;
}

static string ToString(const DrawCommand& cmd, const U16 indent)
{
    string ret = "\n";
    size_t i = 0;
    for (const GenericDrawCommand& drawCmd : cmd._drawCommands) {
        ret.append("    ");
        for (U16 j = 0; j < indent; ++j) {
            ret.append("    ");
        }
        ret.append(Util::StringFormat("{}: Draw count: {} Base instance: {} Instance count: {} Index count: {}\n", i++, drawCmd._drawCount, drawCmd._cmd.baseInstance, drawCmd._cmd.instanceCount, drawCmd._cmd.indexCount));
    }

    return ret;
}

static string ToString(const SetViewportCommand& cmd, [[maybe_unused]] U16 indent)
{
    return Util::StringFormat(" [{}, {}, {}, {}]", cmd._viewport.x, cmd._viewport.y, cmd._viewport.z, cmd._viewport.w);
}

static string ToString(const PushViewportCommand& cmd, [[maybe_unused]] U16 indent)
{
    return Util::StringFormat(" [{}, {}, {}, {}]", cmd._viewport.x, cmd._viewport.y, cmd._viewport.z, cmd._viewport.w);
}

static string ToString(const BeginRenderPassCommand& cmd, U16 indent)
{
    string ret = "\n";
    for ( U16 j = 0; j < indent; ++j )
    {
        ret.append( "    " );
    }
    ret.append(Util::StringFormat(" Name: [ {} ] Target: [ {} ] Mip Level: [ {} ]: \n", cmd._name.c_str(), to_base(cmd._target), cmd._descriptor._mipWriteLevel));

    U8 k = 0u;
    for ( const bool draw : cmd._descriptor._drawMask )
    {
        ret.append( "    " );
        for ( U16 j = 0; j < indent; ++j )
        {
            ret.append( "    " );
        }
        ret.append( Util::StringFormat( "Draw Mask[ {} ]: {}\n", k++, draw ? "TRUE" : "FALSE") );
    }

    k = 0u;
    for ( const DrawLayerEntry& layer : cmd._descriptor._writeLayers )
    {
        ret.append( "    " );
        for ( U16 j = 0; j < indent; ++j )
        {
            ret.append( "    " );
        }
        ret.append( Util::StringFormat( "Write Layer[ {} ]: [slice: {} - {}, face: {}]\n", k++, layer._layer._offset, layer._layer._count, layer._cubeFace ) );
    }

    k = 0u;
    for ( const auto& clear : cmd._clearDescriptor )
    {
        ret.append( "    " );
        for ( U16 j = 0; j < indent; ++j )
        {
            ret.append( "    " );
        }
        ret.append( Util::StringFormat("Clear Colour [ {:.2f}, {:.2f}, {:.2f}, {:.2f} ] (Enabled: {})\n",  clear._colour.r, clear._colour.g, clear._colour.b, clear._colour.a, clear._enabled ? "TRUE" : "FALSE") );
    }

    return ret;
}

static string ToString(const SetScissorCommand& cmd, [[maybe_unused]] U16 indent)
{
    return Util::StringFormat(" [{}, {}, {}, {}]", cmd._rect.x, cmd._rect.y, cmd._rect.z, cmd._rect.w);
}

static string ToString(const SetClipPlanesCommand& cmd, const U16 indent) {
    string ret = "\n";

    auto& planes = cmd._clippingPlanes.planes();
    auto& states = cmd._clippingPlanes.planeState();
    for (U8 i = 0; i < to_U8(ClipPlaneIndex::COUNT); ++i) {
        if (states[i]) {
            ret.append("    ");
            for (U16 j = 0; j < indent; ++j) {
                ret.append("    ");
            }

            const float4& eq = planes[i]._equation;

            ret.append(Util::StringFormat("Plane [{}] [ {:5.2f} {:5.2f} {:5.2f} - {:5.2f} ]\n", i, eq.x, eq.y, eq.z, eq.w));
        }
    }

    return ret;
}

static string ToString(const SetCameraCommand& cmd, [[maybe_unused]] U16 indent) {
    string ret = "    ";
    ret.append(Util::StringFormat("[ Camera position (eye): [ {:5.2f} {:5.2f} {:5.2f}]\n", cmd._cameraSnapshot._eye.x, cmd._cameraSnapshot._eye.y, cmd._cameraSnapshot._eye.z));
    return ret;
}

static string ToString(const BindShaderResourcesCommand& cmd, const U16 indent)
{
    U8 bufferCount = 0u;
    U8 imageCount = 0u;
    for (U8 i = 0u; i < cmd._set._bindingCount; ++i)
    {
        const DescriptorSetBinding& binding = cmd._set._bindings[i];

        if ( binding._data._type == DescriptorSetBindingType::UNIFORM_BUFFER_STATIC ||
             binding._data._type == DescriptorSetBindingType::UNIFORM_BUFFER_DYNAMIC ||
             binding._data._type == DescriptorSetBindingType::SHADER_STORAGE_BUFFER_DYNAMIC ||
             binding._data._type == DescriptorSetBindingType::SHADER_STORAGE_BUFFER_STATIC )
        {
            ++bufferCount;
        }
        else if ( binding._data._type == DescriptorSetBindingType::COMBINED_IMAGE_SAMPLER ||
                  binding._data._type == DescriptorSetBindingType::IMAGE)
        {
            ++imageCount;
        }
    }

    string ret = Util::StringFormat(" [ Buffers: {}, Images: {} ]\n", bufferCount, imageCount);

    for ( U8 i = 0u; i < cmd._set._bindingCount; ++i )
    {
        const DescriptorSetBinding& binding = cmd._set._bindings[i];

        if ( binding._data._type == DescriptorSetBindingType::UNIFORM_BUFFER_STATIC ||
             binding._data._type == DescriptorSetBindingType::UNIFORM_BUFFER_DYNAMIC ||
             binding._data._type == DescriptorSetBindingType::SHADER_STORAGE_BUFFER_DYNAMIC ||
             binding._data._type == DescriptorSetBindingType::SHADER_STORAGE_BUFFER_STATIC )
        {
            ret.append( "    " );
            for (U16 j = 0; j < indent; ++j)
            {
                ret.append("    ");
            }

            ret.append(Util::StringFormat("Buffer [ {} - {} ] Range [{} - {}] Read Index [ {} ]\n",
                       binding._slot,
                       binding._data._buffer._buffer->getGUID(),
                       binding._data._buffer._range._startOffset,
                       binding._data._buffer._range._length,
                       binding._data._buffer._queueReadIndex));
        }
    }

    for ( U8 i = 0u; i < cmd._set._bindingCount; ++i )
    {
        const DescriptorSetBinding& binding = cmd._set._bindings[i];

        if ( binding._data._type == DescriptorSetBindingType::COMBINED_IMAGE_SAMPLER ||
             binding._data._type == DescriptorSetBindingType::IMAGE )
        {
            if (binding._slot == INVALID_TEXTURE_BINDING)
            {
                continue;
            }
            
            ret.append("    ");
            for (U16 j = 0; j < indent; ++j)
            {
                ret.append("    ");
            }

            if ( binding._data._type == DescriptorSetBindingType::COMBINED_IMAGE_SAMPLER )
            {
                const DescriptorCombinedImageSampler& sampledImage = binding._data._sampledImage;
                const Texture* srcTex = sampledImage._image._srcTexture;

                ret.append(Util::StringFormat("Texture [ {} - {} - {} - {} ] Layers: [ {} - {} ] MipRange: [ {} - {} ]\n",
                            binding._slot,
                            srcTex != nullptr ? srcTex->getGUID() : 0u,
                            srcTex != nullptr ? srcTex->resourceName().c_str() : "no-name",
                            GetHash(sampledImage._sampler),
                            sampledImage._image._subRange._layerRange._offset,
                            sampledImage._image._subRange._layerRange._count,
                            sampledImage._image._subRange._mipLevels._offset,
                            sampledImage._image._subRange._mipLevels._count));
            }
            else
            {
                const DescriptorImageView& imageView = binding._data._imageView;
                const Texture* srcTex = imageView._image._srcTexture;

                ret.append(Util::StringFormat("Image binds: Slot [{}] - Src GUID [ {} ] - Src Name [ {} ] - Layers [{} - {}] - Levels [{} - {}] - Flag [ {} ]",
                           binding._slot,
                           srcTex != nullptr ? srcTex->getGUID() : 0u,
                           srcTex != nullptr ? srcTex->resourceName().c_str() : "no-name",
                           imageView._image._subRange._layerRange._offset,
                           imageView._image._subRange._layerRange._count,
                           imageView._image._subRange._mipLevels._offset,
                           imageView._image._subRange._mipLevels._count,
                           Divide::Names::imageUsage[to_base( imageView._usage )] ));
            }
        }
    }

    return ret;
}

static string ToString(const BeginDebugScopeCommand& cmd, [[maybe_unused]] const U16 indent)
{
    return " [ " + string(cmd._scopeName.c_str()) + " ]";
}

static string ToString(const AddDebugMessageCommand& cmd, const U16 indent)
{
    string ret = "\n";
    for (U16 j = 0; j < indent; ++j) {
        ret.append("    ");
    }

    ret.append( " [ " + string(cmd._msg.c_str()) + " ]");
    return ret;
}

static string ToString(const DispatchShaderTaskCommand& cmd, [[maybe_unused]] U16 indent)
{
    return Util::StringFormat(" [ Group sizes: {} {} {}]", cmd._workGroupSize.x, cmd._workGroupSize.y, cmd._workGroupSize.z);
}

static string ToString(const MemoryBarrierCommand& cmd, U16 indent) {
    string ret = Util::StringFormat(" [ Buffer locks: {} ] [ Texture layout changes: {} ]\n",
                                      cmd._bufferLocks.size(),
                                      cmd._textureLayoutChanges.size());
 
    for (auto it : cmd._bufferLocks) {
        ret.append("    ");
        for (U16 j = 0; j < indent; ++j) {
            ret.append("    ");
        }

        const I64 guid = it._buffer != nullptr ? it._buffer->getGUID() : -1;

        ret.append(Util::StringFormat("Buffer lock: [ {} - [{} - {}] ]\n", guid, it._range._startOffset, it._range._length));
    }

    for (auto it : cmd._textureLayoutChanges) {
        ret.append("    ");
        for (U16 j = 0; j < indent; ++j) {
            ret.append("    ");
        }
        ret.append(Util::StringFormat("Texture Layout Change: [ {} [ {} -> {} ]]\n", it._targetView._srcTexture ? it._targetView._srcTexture->getGUID() : -1, Divide::Names::imageUsage[to_base( it._sourceLayout )], Divide::Names::imageUsage[to_base(it._targetLayout)]));
    }
    return ret;
}

static string ToString( const BeginGPUQueryCommand& cmd, [[maybe_unused]] const U16 indent )
{
    string ret = " Bit Mask: ";
    ret.append( std::bitset<32>(cmd._queryMask).to_string() );
    return ret;
}

static string ToString( const EndGPUQueryCommand& cmd, [[maybe_unused]] const U16 indent )
{
    return cmd._waitForResults ? " Wait for results: TRUE" : " Wait for results: FALSE";
}

static string ToString( const BlitRenderTargetCommand& cmd, const U16 indent )
{
    string ret = Util::StringFormat("Source ID [ {} ] Target ID [ {} ] Param count [ {} ]\n", cmd._source, cmd._destination, cmd._params.size());
    for ( auto it : cmd._params )
    {
        ret.append( "    " );
        for ( U16 j = 0; j < indent; ++j )
        {
            ret.append( "    " );
        }

        ret.append( Util::StringFormat( "Input: [l: {} m: {} i: {}] Output: [l: {} m: {} i: {}] Layer count: [ {} ] Mip Count: [ {} ]\n", it._input._layerOffset, it._input._mipOffset, it._input._index, it._output._layerOffset, it._output._mipOffset, it._output._index, it._layerCount, it._mipCount ) );
    }

    return ret;
}

static string ToString( [[maybe_unused]] const CopyTextureCommand& cmd, [[maybe_unused]] const U16 indent )
{
    return " ToDo";
}

static string ToString( [[maybe_unused]] const ReadTextureCommand& cmd, [[maybe_unused]] const U16 indent )
{
    return " ToDo";
}

static string ToString( [[maybe_unused]] const ClearTextureCommand& cmd, [[maybe_unused]] const U16 indent )
{
    return " ToDo";
}

static string ToString( [[maybe_unused]] const ComputeMipMapsCommand& cmd, [[maybe_unused]] const U16 indent )
{
    return " ToDo";
}

static string ToString( [[maybe_unused]] const PushCameraCommand& cmd, [[maybe_unused]] const U16 indent )
{
    return " ToDo";
}

static string ToString( [[maybe_unused]] const ReadBufferDataCommand& cmd, [[maybe_unused]] const U16 indent )
{
    return " ToDo";
}

static string ToString( [[maybe_unused]] const ClearBufferDataCommand& cmd, [[maybe_unused]] const U16 indent )
{
    return " ToDo";
}

string ToString(const CommandBase& cmd, const CommandType type, U16 indent) {
    string ret(indent, ' ');
    ret.append(Names::commandType[to_base( type )]);

    indent += 3u;
    switch ( type ) {
        case CommandType::BEGIN_RENDER_PASS:
        {
            ret.append(ToString(static_cast<const BeginRenderPassCommand&>(cmd), indent));
        }break;
        case CommandType::END_RENDER_PASS: break;
        case CommandType::BEGIN_GPU_QUERY:
        {
            ret.append(ToString(static_cast<const BeginGPUQueryCommand&>(cmd), indent));
        }break;
        case CommandType::END_GPU_QUERY: 
        {
            ret.append(ToString(static_cast<const EndGPUQueryCommand&>(cmd), indent));
        }break;
        case CommandType::SET_VIEWPORT:
        {
            ret.append(ToString(static_cast<const SetViewportCommand&>(cmd), indent));
        }break;
        case CommandType::PUSH_VIEWPORT:
        {
            ret.append(ToString(static_cast<const PushViewportCommand&>(cmd), indent));
        }break;
        case CommandType::POP_VIEWPORT: break;
        case CommandType::SET_SCISSOR:
        {
            ret.append(ToString(static_cast<const SetScissorCommand&>(cmd), indent));
        }break;
        case CommandType::BLIT_RT:
        {
            ret.append(ToString(static_cast<const BlitRenderTargetCommand&>(cmd), indent));
        }break;
        case CommandType::COPY_TEXTURE:
        {
            ret.append( ToString( static_cast<const CopyTextureCommand&>(cmd), indent ) );
        }break;
        case CommandType::READ_TEXTURE:
        {
            ret.append( ToString( static_cast<const ReadTextureCommand&>(cmd), indent ) );
        }break;
        case CommandType::CLEAR_TEXTURE:
        {
            ret.append( ToString( static_cast<const ClearTextureCommand&>(cmd), indent ) );
        }break;
        case CommandType::COMPUTE_MIPMAPS:
        {
            ret.append( ToString( static_cast<const ComputeMipMapsCommand&>(cmd), indent ) );
        }break;
        case CommandType::SET_CAMERA:
        {
            ret.append(ToString(static_cast<const SetCameraCommand&>(cmd), indent));
        }break;
        case CommandType::PUSH_CAMERA:
        {
            ret.append(ToString(static_cast<const PushCameraCommand&>(cmd), indent));
        }break;
        case CommandType::POP_CAMERA: break;
        case CommandType::SET_CLIP_PLANES:
        {
            ret.append(ToString(static_cast<const SetClipPlanesCommand&>(cmd), indent));
        }break;
        case CommandType::BIND_PIPELINE: {
            ret.append(ToString(static_cast<const BindPipelineCommand&>(cmd), indent));
        }break;
        case CommandType::BIND_SHADER_RESOURCES:
        {
            ret.append(ToString(static_cast<const BindShaderResourcesCommand&>(cmd), indent));
        }break;
        case CommandType::SEND_PUSH_CONSTANTS:
        {
            ret.append(ToString(static_cast<const SendPushConstantsCommand&>(cmd), indent));
        }break;
        case CommandType::DRAW_COMMANDS:
        {
            ret.append(ToString(static_cast<const DrawCommand&>(cmd), indent));
        }break;
        case CommandType::DISPATCH_SHADER_TASK:
        {
            ret.append(ToString(static_cast<const DispatchShaderTaskCommand&>(cmd), indent));
        }break;
        case CommandType::MEMORY_BARRIER:
        {
            ret.append(ToString(static_cast<const MemoryBarrierCommand&>(cmd), indent));
        }break;
        case CommandType::READ_BUFFER_DATA:
        {
            ret.append(ToString(static_cast<const ReadBufferDataCommand&>(cmd), indent));
        }break;
        case CommandType::CLEAR_BUFFER_DATA:
        {
            ret.append(ToString(static_cast<const ClearBufferDataCommand&>(cmd), indent));
        }break;
        case CommandType::BEGIN_DEBUG_SCOPE:
        {
            ret.append(ToString(static_cast<const BeginDebugScopeCommand&>(cmd), indent));
        }break; 
        case CommandType::END_DEBUG_SCOPE: break;
        case CommandType::ADD_DEBUG_MESSAGE:
        {
            ret.append(ToString(static_cast<const AddDebugMessageCommand&>(cmd), indent));
        }break;
        default: break;
    }
    return ret;
}

} //namespace GFX
} //namespace Divide
