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
#ifndef _SHADER_PROGRAM_H_
#define _SHADER_PROGRAM_H_

#include "Core/Resources/Headers/Resource.h"
#include "Core/Resources/Headers/ResourceDescriptor.h"
#include "Platform/Video/Headers/GraphicsResource.h"
#include "Platform/Video/Headers/RenderAPIEnums.h"

namespace FW {
    class FileWatcher;
};

namespace Divide {

class Kernel;
class Camera;
class Material;
class ShaderBuffer;
class ResourceCache;
class ShaderProgramDescriptor;

struct PushConstants;
struct Configuration;

enum class FileUpdateEvent : U8;

FWD_DECLARE_MANAGED_CLASS(ShaderProgram);

namespace Attorney {
    class ShaderProgramKernel;
}

using ModuleDefines = vector<std::pair<string, bool>>;


struct ShaderModuleDescriptor {
    ModuleDefines _defines;
    Str64 _sourceFile;
    Str64 _variant;
    ShaderType _moduleType = ShaderType::COUNT;
    bool _batchSameFile = true;
};

void ProcessShadowMappingDefines(const Configuration& config, ModuleDefines& defines);

class ShaderProgramDescriptor final : public PropertyDescriptor {
public:
    ShaderProgramDescriptor() noexcept
        : PropertyDescriptor(DescriptorType::DESCRIPTOR_SHADER) {

    }

    size_t getHash() const noexcept override;

    vector<ShaderModuleDescriptor> _modules;
};

class NOINITVTABLE ShaderProgram : public CachedResource,
                                   public GraphicsResource {
    friend class Attorney::ShaderProgramKernel;

   public:
    struct UniformDeclaration
    {
        Str64 _type;
        Str256 _name;
    };
    using ShaderProgramMapEntry = std::pair<ShaderProgram*, size_t>;
    using ShaderProgramMap = ska::bytell_hash_map<I64 /*handle*/, ShaderProgramMapEntry>;
    using AtomMap = ska::bytell_hash_map<U64 /*name hash*/, string>;
    //using AtomInclusionMap = ska::bytell_hash_map<U64 /*name hash*/, vector<ResourcePath>>;
    using AtomInclusionMap = hashMap<U64 /*name hash*/, vector<ResourcePath>>;
    using UniformsInclusionMap = hashMap<U64 /*name hash*/, vector<UniformDeclaration>>;
    using ShaderQueue = eastl::stack<ShaderProgram*, vector_fast<ShaderProgram*> >;


   public:
    explicit ShaderProgram(GFXDevice& context,
                           size_t descriptorHash,
                           const Str256& shaderName,
                           const Str256& shaderFileName,
                           const ResourcePath& shaderFileLocation,
                           ShaderProgramDescriptor descriptor,
                           bool asyncLoad);
    virtual ~ShaderProgram();

    /// Is the shader ready for drawing?
    virtual bool isValid() const = 0;
    bool load() override;
    bool unload() override;

    virtual bool recompile(bool force, bool& skipped);

    /** ------ BEGIN EXPERIMENTAL CODE ----- **/
    size_t getFunctionCount(const ShaderType shader) noexcept {
        return _functionIndex[to_U32(shader)].size();
    }

    void setFunctionCount(const ShaderType shader, const size_t count) {
        _functionIndex[to_U32(shader)].resize(count, 0);
    }

    void setFunctionIndex(const ShaderType shader, const U32 index, const U32 functionEntry) {
        const U32 shaderTypeValue = to_U32(shader);

        if (_functionIndex[shaderTypeValue].empty()) {
            return;
        }

        DIVIDE_ASSERT(index < _functionIndex[shaderTypeValue].size(),
                      "ShaderProgram error: Invalid function index specified "
                      "for update!");
        if (_availableFunctionIndex[shaderTypeValue].empty()) {
            return;
        }

        DIVIDE_ASSERT(
            functionEntry < _availableFunctionIndex[shaderTypeValue].size(),
            "ShaderProgram error: Specified function entry does not exist!");
        _functionIndex[shaderTypeValue][index] = _availableFunctionIndex[shaderTypeValue][functionEntry];
    }

    U32 addFunctionIndex(const ShaderType shader, const U32 index) {
        const U32 shaderTypeValue = to_U32(shader);

        _availableFunctionIndex[shaderTypeValue].push_back(index);
        return to_U32(_availableFunctionIndex[shaderTypeValue].size() - 1);
    }
    /** ------ END EXPERIMENTAL CODE ----- **/

    //==================== static methods ===============================//
    static void Idle();
    static void OnStartup(ResourceCache* parentCache);
    static void OnShutdown();
    static bool UpdateAll();
    /// Queue a shaderProgram recompile request
    static bool RecompileShaderProgram(const Str256& name);
    /// Remove a shaderProgram from the program cache
    static bool UnregisterShaderProgram(size_t shaderHash);
    /// Add a shaderProgram to the program cache
    static void RegisterShaderProgram(ShaderProgram* shaderProgram);
    /// Find a specific shader program by handle.
    static ShaderProgram* FindShaderProgram(I64 shaderHandle);
    /// Find a specific shader program by descriptor hash.
    static ShaderProgram* FindShaderProgram(size_t shaderHash);

    /// Return a default shader used for general purpose rendering
    static const ShaderProgram_ptr& DefaultShader();

    static const ShaderProgram_ptr& NullShader();

    static void RebuildAllShaders();

    static vector<ResourcePath> GetAllAtomLocations();

    static bool UseShaderTexCache() noexcept { return s_useShaderTextCache; }
    static bool UseShaderBinaryCache() noexcept { return s_useShaderBinaryCache; }

    static size_t DefinesHash(const ModuleDefines& defines);

    static I32 ShaderProgramCount() noexcept { return s_shaderCount.load(std::memory_order_relaxed); }

    const ShaderProgramDescriptor& descriptor() const noexcept { return _descriptor; }

    [[nodiscard]] const char* getResourceTypeName() const noexcept override { return "ShaderProgram"; }

    PROPERTY_RW(bool, highPriority, true);

   protected:
     static void UseShaderTextCache(bool state) noexcept { if (s_useShaderBinaryCache) { state = false; } s_useShaderTextCache = state; }
     static void UseShaderBinaryCache(const bool state) noexcept { s_useShaderBinaryCache = state; if (state) { UseShaderTextCache(false); } }

   protected:
    /// Used to render geometry without valid materials.
    /// Should emulate the basic fixed pipeline functions (no lights, just colour and texture)
    static ShaderProgram_ptr s_imShader;
    /// Pointer to a shader that we will perform operations on
    static ShaderProgram_ptr s_nullShader;
    /// Only 1 shader program per frame should be recompiled to avoid a lot of stuttering
    static ShaderQueue s_recompileQueue;
    /// Shader program cache
    static ShaderProgramMap s_shaderPrograms;
    static std::pair<I64, ShaderProgramMapEntry> s_lastRequestedShaderProgram;
    static SharedMutex s_programLock;

   private:
    std::array<vector<U32>, to_base(ShaderType::COUNT)> _functionIndex;
    std::array<vector<U32>, to_base(ShaderType::COUNT)> _availableFunctionIndex;

   protected:
    template <typename T>
    friend class ImplResourceLoader;

    const ShaderProgramDescriptor _descriptor;

    bool _asyncLoad = true;

    static bool s_useShaderTextCache;
    static bool s_useShaderBinaryCache;
    static std::atomic_int s_shaderCount;
};

namespace Attorney {
    class ShaderProgramKernel {
      protected:
        static void UseShaderTextCache(const bool state) noexcept {
            ShaderProgram::UseShaderTextCache(state);
        }

        static void UseShaderBinaryCache(const bool state) {
            ShaderProgram::UseShaderBinaryCache(state);
        }

        friend class Kernel;
    };
}

};  // namespace Divide
#endif //_SHADER_PROGRAM_H_
