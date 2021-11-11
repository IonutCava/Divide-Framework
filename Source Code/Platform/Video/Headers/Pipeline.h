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

namespace Divide {

enum class MemoryBarrierType : U32 {
    BUFFER_UPDATE = toBit(1),
    SHADER_STORAGE = toBit(2),
    COMMAND_BUFFER = toBit(3),
    ATOMIC_COUNTER = toBit(4),
    QUERY = toBit(5),
    RENDER_TARGET = toBit(6),
    TEXTURE_UPDATE = toBit(7),
    TEXTURE_FETCH = toBit(8),
    SHADER_IMAGE = toBit(9),
    TRANSFORM_FEEDBACK = toBit(10),
    VERTEX_ATTRIB_ARRAY = toBit(11),
    INDEX_ARRAY = toBit(12),
    UNIFORM_DATA = toBit(13),
    PIXEL_BUFFER = toBit(14),
    PERSISTENT_BUFFER = toBit(15),
    ALL_MEM_BARRIERS = PERSISTENT_BUFFER + 256,
    TEXTURE_BARRIER = toBit(16), //This is not included in ALL!
    COUNT = 15
};

struct PipelineDescriptor final : Hashable {
    size_t _stateHash = 0;
    I64 _shaderProgramHandle = 0;
    U8 _multiSampleCount = 0u;

    PipelineDescriptor() = default;
    PipelineDescriptor(const size_t hash, const I64 handle, const U8 sampleCount = 0u) noexcept
        : _stateHash(hash), _shaderProgramHandle(handle), _multiSampleCount(sampleCount)
    {
    }

    size_t getHash() const override;
    bool operator==(const PipelineDescriptor& other) const noexcept;
    bool operator!=(const PipelineDescriptor& other) const noexcept;
}; //struct PipelineDescriptor

class Pipeline {
public:
    explicit Pipeline(const PipelineDescriptor& descriptor);

    I64 shaderProgramHandle() const noexcept {
        return _descriptor._shaderProgramHandle;
    }

    const PipelineDescriptor& descriptor() const noexcept {
        return _descriptor;
    }

    size_t stateHash() const noexcept {
        return _descriptor._stateHash;
    }

    U8 multiSampleCount() const noexcept {
        return _descriptor._multiSampleCount;
    }

    size_t getHash() const noexcept {
        return _cachedHash;
    }

    bool operator==(const Pipeline& other) const noexcept {
        return _cachedHash == other._cachedHash;
    }

    bool operator!=(const Pipeline& other) const noexcept {
        return _cachedHash != other._cachedHash;
    }

private: //data
    size_t _cachedHash = 0;
    PipelineDescriptor _descriptor;
    

}; //class Pipeline

}; //namespace Divide

#endif //_PIPELINE_H_

