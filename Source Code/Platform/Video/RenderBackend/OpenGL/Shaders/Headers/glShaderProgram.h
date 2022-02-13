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

struct TextDumpEntry
{
    Str256 _name;
    string _sourceCode;
};

struct BinaryDumpEntry
{
    Str256 _name;
    GLuint _handle = 0u;
};

struct ValidationEntry
{
    Str256 _name;
    GLuint _handle = 0u;
    UseProgramStageMask _stageMask = UseProgramStageMask::GL_NONE_BIT;
};

enum class ShaderResult : U8 {
    Failed = 0,
    OK,
    StillLoading,
    COUNT
};

namespace Attorney {
    class GLAPIShaderProgram;
};
/// OpenGL implementation of the ShaderProgram entity
class glShaderProgram final : public ShaderProgram, public glObject {
    friend class Attorney::GLAPIShaderProgram;
   public:
    static constexpr bool g_useUniformConstantBuffer = true;

   public:
    explicit glShaderProgram(GFXDevice& context,
                             size_t descriptorHash,
                             const Str256& name,
                             const Str256& assetName,
                             const ResourcePath& assetLocation,
                             const ShaderProgramDescriptor& descriptor,
                             bool asyncLoad);
    ~glShaderProgram();

    static void InitStaticData();
    static void DestroyStaticData();
    static void OnStartup();
    static void OnShutdown();
    static void Idle(PlatformContext& platformContext);

    template<typename StringType> 
    static StringType decorateFileName(const StringType& name) {
        if_constexpr(Config::Build::IS_DEBUG_BUILD) {
            return "DEBUG." + name;
        } else if_constexpr(Config::Build::IS_PROFILE_BUILD) {
            return "PROFILE." + name;
        } else {
            return "RELEASE." + name;
        }
    }

    /// Make sure this program is ready for deletion
    bool unload() override;

    void uploadPushConstants(const PushConstants& constants);

    static void OnAtomChange(std::string_view atomName, FileUpdateEvent evt);
    static const string& ShaderFileRead(const ResourcePath& filePath, const ResourcePath& atomName, bool recurse, vector<ResourcePath>& foundAtoms, bool& wasParsed);
    static const string& ShaderFileReadLocked(const ResourcePath& filePath, const ResourcePath& atomName, bool recurse, vector<ResourcePath>& foundAtoms, bool& wasParsed);

    static bool ShaderFileRead(const ResourcePath& filePath, const ResourcePath& fileName, eastl::string& sourceCodeOut);
    static bool ShaderFileWrite(const ResourcePath& filePath, const ResourcePath& fileName, const char* sourceCode);
    static eastl::string PreprocessIncludes(const ResourcePath& name,
                                            const eastl::string& source,
                                            GLint level,
                                            vector<ResourcePath>& foundAtoms,
                                            bool lock);

    static eastl::string GatherUniformDeclarations(const eastl::string& source, vector<UniformDeclaration>& foundUniforms);

    static void QueueShaderWriteToFile(const string& sourceCode, const Str256& fileName);

  protected:
   struct AtomUniformPair {
       vector<ResourcePath> _atoms;
       vector<UniformDeclaration> _uniforms;
   };

    /// return a list of atom names
   AtomUniformPair loadSourceCode(const Str128& stageName,
                                  const Str8& extension,
                                  const string& header,
                                  size_t definesHash,
                                  bool reloadExisting,
                                  Str256& fileNameOut,
                                  eastl::string& sourceCodeOut) const;

    ShaderResult rebindStages();
    ShaderResult validatePreBind(bool rebind = true);
    void processValidation();
    
    bool recompile(bool& skipped) override;
    /// Creation of a new shader program. Pass in a shader token and use glsw to
    /// load the corresponding effects
    bool load() override;
    /// This should be called in the loading thread, but some issues are still
    /// present, and it's not recommended (yet)
    void threadedLoad(bool reloadExisting);

    /// Returns true if at least one shader linked successfully
    bool reloadShaders(bool reloadExisting);

    /// Bind this shader program (returns false if the program failed validation)
    ShaderResult bind();

    static void ProcessValidationQueue();
    static void DumpShaderTextCacheToDisk(const TextDumpEntry& entry);
    static void DumpShaderBinaryCacheToDisk(const BinaryDumpEntry& entry);

   private:
    GLuint _handle = GLUtil::k_invalidObjectID;

    bool _validationQueued = false;
    bool _stagesBound = false;
    bool _hasUniformBlockBuffer = false;
    vector<glShader*> _shaderStage;

    static I64 s_shaderFileWatcherID;

    /// Shaders loaded from files are kept as atoms
    static SharedMutex s_atomLock;
    static AtomMap s_atoms;
    static AtomInclusionMap s_atomIncludes;

    //extra entry for "common" location
    static ResourcePath shaderAtomLocationPrefix[to_base(ShaderType::COUNT) + 1];
    static Str8 shaderAtomExtensionName[to_base(ShaderType::COUNT) + 1];
    static U64 shaderAtomExtensionHash[to_base(ShaderType::COUNT) + 1];
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
