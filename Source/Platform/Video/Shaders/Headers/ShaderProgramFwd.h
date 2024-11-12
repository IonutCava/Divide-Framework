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
#ifndef DVD_SHADER_PROGRAM_FWD_H_
#define DVD_SHADER_PROGRAM_FWD_H_

#include "Core/Resources/Headers/Resource.h"
#include "Core/Headers/PoolHandle.h"

namespace Divide {
    class ShaderProgram;
    struct PerFileShaderData;

    enum class ShaderResult : U8
    {
        Failed = 0,
        OK,
        COUNT
    };

    struct ModuleDefine
    {
        string _define;
        bool _addPrefix{true};
    };

    using ModuleDefines = vector<ModuleDefine>;

    struct ShaderModuleDescriptor
    {
        ShaderModuleDescriptor() = default;
        explicit ShaderModuleDescriptor(ShaderType type, const Str<64>& file, const Str<64>& variant = "");

        ModuleDefines _defines;
        Str<64> _sourceFile;
        Str<64> _variant;
        ShaderType _moduleType = ShaderType::COUNT;
    };

    template<>
    struct PropertyDescriptor<ShaderProgram>
    {
        Str<256> _name;
        ModuleDefines _globalDefines;
        vector<ShaderModuleDescriptor> _modules;
        bool _useShaderCache{true};
    };

    using ShaderProgramDescriptor = PropertyDescriptor<ShaderProgram>;

    inline size_t GetHash(const ShaderModuleDescriptor& moduleDescriptor) noexcept;

    template<>
    inline size_t GetHash( const PropertyDescriptor<ShaderProgram>& descriptor ) noexcept;

    struct ShaderProgramMapEntry
    {
        Handle<ShaderProgram> _program = INVALID_HANDLE<ShaderProgram>;
        U8 _generation = 0u;
    };

    bool operator==(const ShaderProgramMapEntry& lhs, const ShaderProgramMapEntry& rhs) noexcept;
    bool operator!=(const ShaderProgramMapEntry& lhs, const ShaderProgramMapEntry& rhs) noexcept;

}; //namespace Divide

#endif //DVD_SHADER_PROGRAM_FWD_H_

#include "ShaderProgramFwd.inl"
