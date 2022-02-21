#include "stdafx.h"

#include "Headers/Pipeline.h"

#include "Geometry/Material/Headers/ShaderComputeQueue.h"
#include "Scenes/Headers/SceneShaderData.h"

namespace Divide {

size_t GetHash(const PipelineDescriptor& descriptor) {
    size_t hash = descriptor._stateHash;
    Util::Hash_combine(hash, descriptor._multiSampleCount);
    Util::Hash_combine(hash, descriptor._shaderProgramHandle);
    Util::Hash_combine(hash, descriptor._primitiveTopology);

    for (U8 i = 0u; i < to_base(ShaderType::COUNT); ++i) {
        Util::Hash_combine(hash, i);
    }

    Util::Hash_combine(hash, GetHash(descriptor._blendStates));
    for (const AttributeDescriptor& attrDescriptor : descriptor._vertexFormat) {
        Util::Hash_combine(hash, GetHash(attrDescriptor));
    }

    return hash;
}

bool operator==(const PipelineDescriptor& lhs, const PipelineDescriptor& rhs) {
    return lhs._stateHash == rhs._stateHash &&
           lhs._multiSampleCount == rhs._multiSampleCount &&
           lhs._shaderProgramHandle == rhs._shaderProgramHandle &&
           lhs._primitiveTopology == rhs._primitiveTopology &&
           lhs._blendStates == rhs._blendStates &&
           lhs._vertexFormat == rhs._vertexFormat;
}

bool operator!=(const PipelineDescriptor& lhs, const PipelineDescriptor& rhs) {
    return lhs._stateHash != rhs._stateHash ||
           lhs._multiSampleCount != rhs._multiSampleCount ||
           lhs._shaderProgramHandle != rhs._shaderProgramHandle ||
           lhs._primitiveTopology != rhs._primitiveTopology ||
           lhs._blendStates != rhs._blendStates ||
           lhs._vertexFormat != rhs._vertexFormat;
}

Pipeline::Pipeline(const PipelineDescriptor& descriptor)
    : _descriptor(descriptor)
    , _hash(GetHash(descriptor))
{
    _vertexFormatHash = 1337;
    for (const AttributeDescriptor& attrDescriptor : descriptor._vertexFormat) {
        if (attrDescriptor._dataType != GFXDataFormat::COUNT) {
            Util::Hash_combine(_vertexFormatHash, GetHash(attrDescriptor));
        }
    }
}

}; //namespace Divide