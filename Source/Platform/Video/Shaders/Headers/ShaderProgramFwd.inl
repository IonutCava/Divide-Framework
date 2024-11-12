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
#ifndef DVD_SHADER_PROGRAM_FWD_INL_
#define DVD_SHADER_PROGRAM_FWD_INL_

namespace Divide
{
    [[nodiscard]] inline size_t DefinesHash( const ModuleDefines& defines ) noexcept
    {
        size_t hash = 9999991u;

        for ( const auto& [defineString, appendPrefix] : defines )
        {
            Util::Hash_combine( hash, defineString );
            Util::Hash_combine( hash, appendPrefix );
        }

        return hash;
    }

    inline size_t GetHash(const ShaderModuleDescriptor& moduleDescriptor) noexcept
    {
        size_t hash = DefinesHash(moduleDescriptor._defines);

        Util::Hash_combine( hash,
                            moduleDescriptor._variant,
                            moduleDescriptor._sourceFile,
                            moduleDescriptor._moduleType
                          );
        return hash;
    }

    template<>
    inline size_t GetHash( const PropertyDescriptor<ShaderProgram>& descriptor ) noexcept
    {
        size_t hash = DefinesHash(descriptor._globalDefines);

        for ( const ShaderModuleDescriptor& desc : descriptor._modules )
        {
            Util::Hash_combine( hash, GetHash( desc) );
        }

        return hash;
    }
} //namespace Divide

#endif //DVD_SHADER_PROGRAM_FWD_INL_
