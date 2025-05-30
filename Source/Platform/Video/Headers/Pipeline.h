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
#ifndef DVD_PIPELINE_H_
#define DVD_PIPELINE_H_

#include "Platform/Video/Headers/RenderStateBlock.h"
#include "Platform/Video/Headers/BlendingProperties.h"
#include "Platform/Video/Headers/AttributeDescriptor.h"
#include "Platform/Video/Shaders/Headers/ShaderProgramFwd.h"

namespace Divide {

struct PipelineDescriptor
{
    RTBlendStates _blendStates;
    RenderStateBlock _stateBlock{};
    Handle<ShaderProgram> _shaderProgramHandle{ INVALID_HANDLE<ShaderProgram> };
    PrimitiveTopology _primitiveTopology{ PrimitiveTopology::COUNT };
    AttributeMap _vertexFormat;
    U8   _multiSampleCount{ 0u };
    bool _alphaToCoverage{false};
};

size_t GetHash( const PipelineDescriptor& descriptor );
bool operator==(const PipelineDescriptor& lhs, const PipelineDescriptor& rhs);
bool operator!=(const PipelineDescriptor& lhs, const PipelineDescriptor& rhs);

class Pipeline
{
public:
    explicit Pipeline(const PipelineDescriptor& descriptor );


    PROPERTY_R_IW(PipelineDescriptor, descriptor);
    PROPERTY_R_IW(size_t, stateHash, 0u);
    PROPERTY_R_IW(size_t, blendStateHash, 0u);
    PROPERTY_R_IW(size_t, vertexFormatHash, 0u);
    /// Used by Vulkan. It's the complete pipeline hash minus dynamic state settings
    PROPERTY_R_IW(size_t, compiledPipelineHash, 0u);
}; //class Pipeline

bool operator==(const Pipeline& lhs, const Pipeline& rhs) noexcept;
bool operator!=(const Pipeline& lhs, const Pipeline& rhs) noexcept;

}; //namespace Divide

#endif //DVD_PIPELINE_H_
