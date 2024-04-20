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
#ifndef GL_SHADER_PROGRAM_H
#define GL_SHADER_PROGRAM_H

#include "glShader.h"
#include "AI/ActionInterface/Headers/AIProcessor.h"
#include "Platform/Video/GLIM/Declarations.h"

namespace Divide {

class GL_API;
class PlatformContext;

struct PushConstantsStruct;

struct ValidationEntry
{
    Str<256> _name{};
    gl::GLuint _handle{ GL_NULL_HANDLE };
    gl::UseProgramStageMask _stageMask{ gl::UseProgramStageMask::GL_NONE_BIT };
};

namespace Attorney {
    class GLAPIShaderProgram;
};
/// OpenGL implementation of the ShaderProgram entity
class glShaderProgram final : public ShaderProgram {
    friend class Attorney::GLAPIShaderProgram;
   public:
     using glShaders = eastl::fixed_vector<glShaderEntry, to_base( ShaderType::COUNT ), false>;

   public:
    explicit glShaderProgram(GFXDevice& context,
                             size_t descriptorHash,
                             const std::string_view name,
                             std::string_view assetName,
                             const ResourcePath& assetLocation,
                             const ShaderProgramDescriptor& descriptor,
                             ResourceCache& parentCache);

    ~glShaderProgram() override;

    static void Idle(PlatformContext& platformContext);

    /// Make sure this program is ready for deletion
    bool unload() override;

  protected:
    [[nodiscard]] ShaderResult validatePreBind(bool rebind = true) override;
    void processValidation();

    /// Returns true if at least one shader linked successfully
    bool loadInternal(hashMap<U64, PerFileShaderData>& fileData, bool overwrite) override;

    /// Bind this shader program (returns false if the program failed validation)
    ShaderResult bind();

    void uploadPushConstants(const PushConstantsStruct& pushConstants);

    static void ProcessValidationQueue();

   private:
   gl::GLuint _glHandle = GL_NULL_HANDLE;

    bool _validationQueued = false;
    bool _stagesBound = false;
    glShaders _shaderStage;
};

namespace Attorney
{
    class GLAPIShaderProgram
    {
        static ShaderResult bind(glShaderProgram& program)
        {
            return program.bind();
        }

        static void uploadPushConstants(glShaderProgram& program, const PushConstantsStruct& pushConstants)
        {
            program.uploadPushConstants(pushConstants);
        }
        friend class Divide::GL_API;
    };
};  // namespace Attorney
};  // namespace Divide

#endif  //GL_SHADER_PROGRAM_H

