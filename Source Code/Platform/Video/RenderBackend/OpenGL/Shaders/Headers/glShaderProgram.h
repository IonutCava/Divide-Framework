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
#ifndef _PLATFORM_VIDEO_OPENGLS_PROGRAM_H_
#define _PLATFORM_VIDEO_OPENGLS_PROGRAM_H_

#include "glShaderProgram.h"
#include "AI/ActionInterface/Headers/AIProcessor.h"
#include "Platform/Video/GLIM/Declarations.h"
#include "Platform/Video/RenderBackend/OpenGL/Headers/glResources.h"
#include "Platform/Video/Shaders/Headers/ShaderProgram.h"

namespace Divide {
class PlatformContext;
class GL_API;
class glShader;
class glLockManager;

struct ValidationEntry
{
    Str256 _name{};
    GLuint _handle{0u};
    UseProgramStageMask _stageMask{ UseProgramStageMask::GL_NONE_BIT };
};

enum class ShaderResult : U8 {
    Failed = 0,
    OK,
    COUNT
};

namespace Attorney {
    class GLAPIShaderProgram;
};
/// OpenGL implementation of the ShaderProgram entity
class glShaderProgram final : public ShaderProgram, public glObject {
    friend class Attorney::GLAPIShaderProgram;

   public:
    explicit glShaderProgram(GFXDevice& context,
                             size_t descriptorHash,
                             const Str256& name,
                             const Str256& assetName,
                             const ResourcePath& assetLocation,
                             const ShaderProgramDescriptor& descriptor,
                             ResourceCache& parentCache);

    ~glShaderProgram();

    static void Idle(PlatformContext& platformContext);
    static void InitStaticData();
    static void DestroyStaticData();

    /// Make sure this program is ready for deletion
    bool unload() override;

  protected:
    ShaderResult validatePreBind(bool rebind = true);
    void processValidation();
    
    bool recompile(bool& skipped) override;
    /// This should be called in the loading thread, but some issues are still
    /// present, and it's not recommended (yet)
    void threadedLoad(bool reloadExisting) override;

    /// Returns true if at least one shader linked successfully
    bool reloadShaders(hashMap<U64, PerFileShaderData>& fileData, bool reloadExisting) override;

    /// Bind this shader program (returns false if the program failed validation)
    ShaderResult bind();

    static void ProcessValidationQueue();

   private:
    GLuint _handle = GLUtil::k_invalidObjectID;

    bool _validationQueued = false;
    bool _stagesBound = false;
    vector<glShader*> _shaderStage;
};

namespace Attorney {
    class GLAPIShaderProgram {
        static ShaderResult bind(glShaderProgram& program) {
            return program.bind();
        }

        friend class GL_API;
    };
};  // namespace Attorney
};  // namespace Divide

#endif  //_PLATFORM_VIDEO_OPENGLS_PROGRAM_H_

#include "glShaderProgram.inl"
