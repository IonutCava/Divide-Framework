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
#ifndef _SHADER_PROGRAM_FWD_H_
#define _SHADER_PROGRAM_FWD_H_

#include "Platform/Video/Headers/RenderAPIEnums.h"
#include "Core/Resources/Headers/ResourceDescriptor.h"

#include "Core/Headers/ObjectPool.h"

namespace Divide {
    class ShaderProgram;

    enum class ShaderResult : U8 {
        Failed = 0,
        OK,
        COUNT
    };

    struct ModuleDefine {
        ModuleDefine() = default;
        ModuleDefine(const char* define, const bool addPrefix = true) : ModuleDefine(string{ define }, addPrefix) {}
        ModuleDefine(const string& define, const bool addPrefix = true) : _define(define), _addPrefix(addPrefix) {}

        string _define;
        bool _addPrefix = true;
    };

    using ModuleDefines = vector<ModuleDefine>;

    struct ShaderModuleDescriptor {
        ShaderModuleDescriptor() = default;
        explicit ShaderModuleDescriptor(ShaderType type, const Str64& file, const Str64& variant = "")
            : _moduleType(type), _sourceFile(file), _variant(variant)
        {
        }

        ModuleDefines _defines;
        Str64 _sourceFile;
        Str64 _variant;
        ShaderType _moduleType = ShaderType::COUNT;
    };

    struct PerFileShaderData;
    class ShaderProgramDescriptor final : public PropertyDescriptor {
    public:
        ShaderProgramDescriptor() noexcept
            : PropertyDescriptor(DescriptorType::DESCRIPTOR_SHADER)
        {
        }

        size_t getHash() const override;
        Str256 _name;
        ModuleDefines _globalDefines;
        vector<ShaderModuleDescriptor> _modules;
    };

    struct ShaderProgramMapEntry {
        ShaderProgram* _program = nullptr;
        U8 _generation = 0u;
    };

    inline bool operator==(const ShaderProgramMapEntry& lhs, const ShaderProgramMapEntry& rhs) noexcept {
        return lhs._generation == rhs._generation &&
            lhs._program == rhs._program;
    }

    inline bool operator!=(const ShaderProgramMapEntry& lhs, const ShaderProgramMapEntry& rhs) noexcept {
        return lhs._generation != rhs._generation ||
            lhs._program != rhs._program;
    }

    using ShaderProgramHandle = PoolHandle;


    static constexpr ShaderProgramHandle SHADER_INVALID_HANDLE{ U16_MAX, U8_MAX };

}; //namespace Divide

#endif //_SHADER_PROGRAM_FWD_H_