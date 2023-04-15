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

#include "Core/Resources/Headers/ResourceDescriptor.h"

#include "Core/Headers/PoolHandle.h"

namespace Divide {
    class ShaderProgram;
    struct PerFileShaderData;

    using ShaderProgramHandle = PoolHandle;
    static constexpr ShaderProgramHandle SHADER_INVALID_HANDLE{ U16_MAX, U8_MAX };

    enum class ShaderResult : U8 {
        Failed = 0,
        OK,
        COUNT
    };

    struct ModuleDefine {
        ModuleDefine() = default;
        ModuleDefine(const char* define, const bool addPrefix = true);
        ModuleDefine(const string& define, const bool addPrefix = true);

        string _define;
        bool _addPrefix = true;
    };

    using ModuleDefines = vector<ModuleDefine>;

    struct ShaderModuleDescriptor {
        ShaderModuleDescriptor() = default;
        explicit ShaderModuleDescriptor(ShaderType type, const Str64& file, const Str64& variant = "");

        ModuleDefines _defines;
        Str64 _sourceFile;
        Str64 _variant;
        ShaderType _moduleType = ShaderType::COUNT;
    };

    struct ShaderProgramDescriptor final : public PropertyDescriptor {
        ShaderProgramDescriptor() noexcept;

        size_t getHash() const noexcept override;
        Str256 _name;
        ModuleDefines _globalDefines;
        vector<ShaderModuleDescriptor> _modules;
    };

    struct ShaderProgramMapEntry {
        ShaderProgram* _program = nullptr;
        U8 _generation = 0u;
    };

    bool operator==(const ShaderProgramMapEntry& lhs, const ShaderProgramMapEntry& rhs) noexcept;
    bool operator!=(const ShaderProgramMapEntry& lhs, const ShaderProgramMapEntry& rhs) noexcept;

}; //namespace Divide

#endif //_SHADER_PROGRAM_FWD_H_