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
#ifndef GL_SHADER_H
#define GL_SHADER_H

#include "Platform/Video/RenderBackend/OpenGL/Headers/glResources.h"
#include "Platform/Video/Shaders/Headers/ShaderProgram.h"

namespace Divide {

enum class ShaderResult : U8;

struct PushConstantsStruct;

/// glShader represents one of a program's rendering stages (vertex, geometry, fragment, etc)
/// It can be used simultaneously in multiple programs/pipelines
class glShader final : public ShaderModule {
   public:
    /// The shader's name is the period-separated list of properties, type is
    /// the render stage this shader is used for
    explicit glShader(GFXDevice& context, const Str256& name);
    ~glShader();

    [[nodiscard]] bool load(const ShaderProgram::ShaderLoadData& data);

    /// Add or refresh a shader from the cache
    [[nodiscard]] static glShader* LoadShader(GFXDevice& context,
                                              const Str256& name,
                                              bool overwriteExisting,
                                              ShaderProgram::ShaderLoadData& data);

    PROPERTY_R_IW(UseProgramStageMask, stageMask, UseProgramStageMask::GL_NONE_BIT);
    PROPERTY_R_IW(GLuint, handle, GLUtil::k_invalidObjectID);

   private:
    friend class glShaderProgram;
    [[nodiscard]] ShaderResult uploadToGPU(GLuint parentProgramHandle);

    void onParentValidation();

    void uploadPushConstants(const PushConstantsStruct& pushConstants);

  private:
    ShaderProgram::ShaderLoadData _loadData;
    vector<GLuint> _shaderIDs;
    bool _linked = false;
    GLint _pushConstantsLocation{-1};
};

};  // namespace Divide

#endif //GL_SHADER_H