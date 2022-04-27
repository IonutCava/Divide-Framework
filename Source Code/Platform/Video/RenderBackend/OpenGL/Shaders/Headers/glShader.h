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
#ifndef _GL_SHADER_H_
#define _GL_SHADER_H_

#include "Platform/Video/RenderBackend/OpenGL/Headers/glResources.h"
#include "Platform/Video/Shaders/Headers/ShaderProgram.h"

namespace Divide {

enum class ShaderResult : U8;

/// glShader represents one of a program's rendering stages (vertex, geometry, fragment, etc)
/// It can be used simultaneously in multiple programs/pipelines
class glShader final : public GUIDWrapper, public GraphicsResource, public glObject {
   public:
    using ShaderMap = ska::bytell_hash_map<U64, glShader*>;


   public:
    /// The shader's name is the period-separated list of properties, type is
    /// the render stage this shader is used for
    explicit glShader(GFXDevice& context, const Str256& name);
    ~glShader();

    [[nodiscard]] bool load(const ShaderProgram::ShaderLoadData& data);

    void AddRef() noexcept { _refCount.fetch_add(1); }
    /// Returns true if ref count reached 0
    size_t SubRef() noexcept { return _refCount.fetch_sub(1); }

    [[nodiscard]] size_t GetRef() const noexcept { return _refCount.load(); }

    PROPERTY_R(Str256, name);
    PROPERTY_R(bool, valid, false);
    PROPERTY_R_IW(UseProgramStageMask, stageMask, UseProgramStageMask::GL_NONE_BIT);
    PROPERTY_R_IW(GLuint, handle, GLUtil::k_invalidObjectID);

   public:
    // ======================= static data ========================= //
    /// Remove a shader from the cache
    static void RemoveShader(glShader*& s, bool force = false);
    /// Return a new shader reference
    static glShader* GetShader(const Str256& name);

    /// Add or refresh a shader from the cache
    static glShader* LoadShader(GFXDevice& context,
                                const Str256& name,
                                bool overwriteExisting,
                                ShaderProgram::ShaderLoadData& data);


    static void InitStaticData();
    static void DestroyStaticData();

   private:
    friend class glShaderProgram;
    [[nodiscard]] ShaderResult uploadToGPU(GLuint parentProgramHandle);

    void onParentValidation();

  private:
    ShaderProgram::ShaderLoadData _loadData;
    /// A list of preprocessor defines (if the bool in the pair is true, #define is automatically added
    vector<ModuleDefine> _definesList;
    std::atomic_size_t _refCount;
    vector<GLuint> _shaderIDs;
    bool _linked = false;

   private:
    /// Shader cache
    static ShaderMap s_shaderNameMap;
    static SharedMutex s_shaderNameLock;
};

};  // namespace Divide

#endif //_GL_SHADER_H_