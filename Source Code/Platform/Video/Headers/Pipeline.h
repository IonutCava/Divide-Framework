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
#ifndef _PIPELINE_H_
#define _PIPELINE_H_

#include "Core/Headers/Hashable.h"
#include "Platform/Video/Headers/BlendingProperties.h"
#include "Platform/Video/Shaders/Headers/ShaderProgram.h"

namespace Divide {

struct PipelineDescriptor {
    RTBlendStates _blendStates;
    size_t _stateHash = 0;
    ShaderProgram::Handle _shaderProgramHandle = ShaderProgram::INVALID_HANDLE;
    PrimitiveTopology _primitiveTopology = PrimitiveTopology::COUNT;
    AttributeMap _vertexFormat;
    U8 _multiSampleCount = 0u;
    bool _primitiveRestartEnabled = false;
}; //struct PipelineDescriptor

size_t GetHash(const PipelineDescriptor& descriptor);
bool operator==(const PipelineDescriptor& lhs, const PipelineDescriptor& rhs);
bool operator!=(const PipelineDescriptor& lhs, const PipelineDescriptor& rhs);

class Pipeline {
public:
    explicit Pipeline(const PipelineDescriptor& descriptor);

    PROPERTY_R_IW(PipelineDescriptor, descriptor);
    PROPERTY_R_IW(size_t, hash, 0u);

    PROPERTY_R_IW(size_t, vertexFormatHash, 0u);
}; //class Pipeline

inline bool operator==(const Pipeline& lhs, const Pipeline& rhs) noexcept {
    return lhs.hash() == rhs.hash();
}

inline bool operator!=(const Pipeline& lhs, const Pipeline& rhs) noexcept {
    return lhs.hash() != rhs.hash();
}

}; //namespace Divide

#endif //_PIPELINE_H_

