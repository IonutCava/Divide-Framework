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
#ifndef _GL_UNIFORM_PUSH_CONSTANT_UPLOADER_H_
#define _GL_UNIFORM_PUSH_CONSTANT_UPLOADER_H_

#include "glPushConstantUploader.h"

namespace Divide {
    struct glUniformPushConstantUploader final : glPushConstantUploader
    {
        explicit glUniformPushConstantUploader(GLuint programHandle);

        void uploadPushConstant(const GFX::PushConstant& constant, bool force = false) override;
        void cacheUniforms() override;
        void commit() override;
        void prepare() override;

    private:
        void uniform(I32 binding, GFX::PushConstantType type, const Byte* values, GLsizei byteCount) const;
        [[nodiscard]] I32  cachedValueUpdate(const GFX::PushConstant& constant, bool force = false);

    private:
        template<typename T>
        struct UniformCache
        {
            using ShaderVarMap = hashMap<T, GFX::PushConstant>;
            void clear() { _shaderVars.clear(); }
            ShaderVarMap _shaderVars;
        };
        using UniformsByNameHash = UniformCache<U64>;
        using ShaderVarMap = hashMap<U64, I32>;

        ShaderVarMap _shaderVarLocation;
        UniformsByNameHash _uniformsByNameHash;
    };
} // namespace Divide

#endif //_GL_UNIFORM_PUSH_CONSTANT_UPLOADER_H_
