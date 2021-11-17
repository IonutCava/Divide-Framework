#include "stdafx.h"

#include "Headers/GFXShaderData.h"
#include "Platform/Video/Buffers/ShaderBuffer/Headers/ShaderBuffer.h"

namespace Divide {
	bool ValidateGPUDataStructure() noexcept {
		constexpr size_t dataSize = sizeof(GFXShaderData::GPUData);
		const size_t alignmentRequirement = ShaderBuffer::AlignmentRequirement(ShaderBuffer::Usage::CONSTANT_BUFFER);
		return dataSize % alignmentRequirement == 0;
	}
}; //namespace Divide