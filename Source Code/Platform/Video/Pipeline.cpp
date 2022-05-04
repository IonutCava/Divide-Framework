#include "stdafx.h"

#include "Headers/Pipeline.h"

#include "Geometry/Material/Headers/ShaderComputeQueue.h"
#include "Scenes/Headers/SceneShaderData.h"

namespace Divide {

size_t GetHash(const PipelineDescriptor& descriptor) {
    size_t hash = descriptor._stateHash;
    Util::Hash_combine(hash, descriptor._multiSampleCount,
                             descriptor._shaderProgramHandle._generation,
                             descriptor._shaderProgramHandle._id);

    for (U8 i = 0u; i < to_base(ShaderType::COUNT); ++i) {
        Util::Hash_combine(hash, i);
    }

    Util::Hash_combine(hash, GetHash(descriptor._blendStates));


    return hash;
}

bool operator==(const PipelineDescriptor& lhs, const PipelineDescriptor& rhs) {
    return lhs._stateHash == rhs._stateHash &&
           lhs._multiSampleCount == rhs._multiSampleCount &&
           lhs._shaderProgramHandle == rhs._shaderProgramHandle &&
           lhs._blendStates == rhs._blendStates;
}

bool operator!=(const PipelineDescriptor& lhs, const PipelineDescriptor& rhs) {
    return lhs._stateHash != rhs._stateHash ||
           lhs._multiSampleCount != rhs._multiSampleCount ||
           lhs._shaderProgramHandle != rhs._shaderProgramHandle ||
           lhs._blendStates != rhs._blendStates;
}

Pipeline::Pipeline(const PipelineDescriptor& descriptor)
    : _descriptor(descriptor)
    , _hash(GetHash(descriptor))
{
}

}; //namespace Divide