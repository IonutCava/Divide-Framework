#include "stdafx.h"

#include "Headers/GFXShaderData.h"
#include "Platform/Video/Buffers/ShaderBuffer/Headers/ShaderBuffer.h"

namespace Divide {
    bool ValidateGPUDataStructure() noexcept {
        constexpr size_t dataSizeA = sizeof(GFXShaderData::CamData);
        constexpr size_t dataSizeB = sizeof(GFXShaderData::RenderData);
        const size_t alignmentRequirement = ShaderBuffer::AlignmentRequirement(ShaderBuffer::Usage::CONSTANT_BUFFER);
        return (dataSizeA % alignmentRequirement == 0) && (dataSizeB % alignmentRequirement == 0);
    }
}; //namespace Divide