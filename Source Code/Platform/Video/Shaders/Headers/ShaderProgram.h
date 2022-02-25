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

struct ModuleDefine {
    ModuleDefine() = default;
    ModuleDefine(const char* define, const bool addPrefix) : ModuleDefine(string{ define }, addPrefix) {}
    ModuleDefine(const string& define, const bool addPrefix) : _define(define), _addPrefix(addPrefix) {}

    string _define;
    bool _addPrefix = true;
};

using ModuleDefines = vector<ModuleDefine>;

struct ShaderModuleDescriptor {
    ModuleDefines _defines;
    Str64 _sourceFile;
    Str64 _variant;
    ShaderType _moduleType = ShaderType::COUNT;
    bool _batchSameFile = true;
};

class ShaderProgramDescriptor final : public PropertyDescriptor {
public:
    ShaderProgramDescriptor() noexcept
        : PropertyDescriptor(DescriptorType::DESCRIPTOR_SHADER) {

    }

    size_t getHash() const override;
    Str256 _name;
    vector<ShaderModuleDescriptor> _modules;
};

struct ShaderProgramMapEntry {
    ShaderProgram* _program = nullptr;
    U8 _generation = 0u;
};

inline bool operator==(const ShaderProgramMapEntry& lhs, const ShaderProgramMapEntry& rhs) noexcept {
    return lhs._generation == rhs._generation &&
           lhs._program == rhs._program;
}

inline bool operator!=(const ShaderProgramMapEntry& lhs, const ShaderProgramMapEntry& rhs) noexcept {
    return lhs._generation != rhs._generation ||
           lhs._program != rhs._program;
}

class NOINITVTABLE ShaderProgram : public CachedResource,
                                   public GraphicsResource {
    friend class Attorney::ShaderProgramKernel;
   public:
    using Handle = PoolHandle;
    static constexpr Handle INVALID_HANDLE{ U16_MAX, U8_MAX };

    static bool s_UseBindlessTextures;

    struct UniformDeclaration
    {
        Str64 _type;
        Str256 _name;
    };
    struct AtomUniformPair {
        vector<ResourcePath> _atoms;
        vector<UniformDeclaration> _uniforms;
    };
    struct TextDumpEntry
    {
        Str256 _name;
        string _sourceCode;
    };

     using ShaderProgramMap = std::array<ShaderProgramMapEntry, U16_MAX>;

    using AtomMap = ska::bytell_hash_map<U64 /*name hash*/, string>;
    //using AtomInclusionMap = ska::bytell_hash_map<U64 /*name hash*/, vector<ResourcePath>>;
    using AtomInclusionMap = hashMap<U64 /*name hash*/, vector<ResourcePath>>;
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

    bool load() override;
    bool unload() override;

    inline bool recompile() {
        bool skipped = false;
        return recompile(skipped);
    }

    virtual bool recompile(bool& skipped);

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
    static void Idle(PlatformContext& platformContext);
    [[nodiscard]] static ErrorCode OnStartup(ResourceCache* parentCache);
    [[nodiscard]] static ErrorCode PostInitAPI(ResourceCache* parentCache);
    [[nodiscard]] static bool OnShutdown();
    [[nodiscard]] static bool OnThreadCreated(const GFXDevice& gfx, const std::thread::id& threadID);
    /// Queue a shaderProgram recompile request
    static bool RecompileShaderProgram(const Str256& name);
    /// Remove a shaderProgram from the program cache
    static bool UnregisterShaderProgram(Handle shaderHandle);
    /// Add a shaderProgram to the program cache
    static void RegisterShaderProgram(ShaderProgram* shaderProgram);
    /// Find a specific shader program by handle.
    [[nodiscard]] static ShaderProgram* FindShaderProgram(Handle shaderHandle);

    /// Return a default shader used for general purpose rendering
    [[nodiscard]] static const ShaderProgram_ptr& DefaultShader() noexcept;
    /// Return a default shader used for general purpose rendering in the main rendering pass
    [[nodiscard]] static const ShaderProgram_ptr& DefaultShaderWorld() noexcept;
    /// Return a default shader used for general purpose rendering in the main OIT pass
    [[nodiscard]] static const ShaderProgram_ptr& DefaultShaderOIT() noexcept;

    [[nodiscard]] static const ShaderProgram_ptr& NullShader() noexcept;
    [[nodiscard]] const I64 NullShaderGUID() noexcept;

    static void RebuildAllShaders();

    [[nodiscard]] static vector<ResourcePath> GetAllAtomLocations();

    [[nodiscard]] static bool UseShaderTexCache() noexcept { return s_useShaderTextCache; }
    [[nodiscard]] static bool UseShaderBinaryCache() noexcept { return s_useShaderBinaryCache; }

    [[nodiscard]] static size_t DefinesHash(const ModuleDefines& defines);

    [[nodiscard]] static I32 ShaderProgramCount() noexcept { return s_shaderCount.load(std::memory_order_relaxed); }

    [[nodiscard]] const ShaderProgramDescriptor& descriptor() const noexcept { return _descriptor; }

    [[nodiscard]] const char* getResourceTypeName() const noexcept override { return "ShaderProgram"; }

    static void OnAtomChange(std::string_view atomName, FileUpdateEvent evt);
    static void QueueShaderWriteToFile(const string& sourceCode, const Str256& fileName);

    template<typename StringType> 
    inline static StringType DecorateFileName(const StringType& name) {
        if_constexpr(Config::Build::IS_DEBUG_BUILD) {
            return "DEBUG." + name;
        } else if_constexpr(Config::Build::IS_PROFILE_BUILD) {
            return "PROFILE." + name;
        } else {
            return "RELEASE." + name;
        }
    }

    PROPERTY_RW(bool, highPriority, true);
    PROPERTY_R_IW(Handle, handle, INVALID_HANDLE);

   protected:
     static void UseShaderTextCache(bool state) noexcept { if (s_useShaderBinaryCache) { state = false; } s_useShaderTextCache = state; }
     static void UseShaderBinaryCache(const bool state) noexcept { s_useShaderBinaryCache = state; if (state) { UseShaderTextCache(false); } }

   protected:
    /// Used to render geometry without valid materials.
    /// Should emulate the basic fixed pipeline functions (no lights, just colour and texture)
    static ShaderProgram_ptr s_imShader;
    static ShaderProgram_ptr s_imWorldShader;
    static ShaderProgram_ptr s_imWorldOITShader;
    /// Pointer to a shader that we will perform operations on
    static ShaderProgram_ptr s_nullShader;
    /// Only 1 shader program per frame should be recompiled to avoid a lot of stuttering
    static ShaderQueue s_recompileQueue;
    /// Shader program cache
    static ShaderProgramMap s_shaderPrograms;

    struct LastRequestedShader {
        ShaderProgram* _program = nullptr;
        Handle _handle = INVALID_HANDLE;
    };
    static LastRequestedShader s_lastRequestedShaderProgram;
    static Mutex s_programLock;

protected:
    virtual void threadedLoad(bool reloadExisting);
    virtual void onAtomChangeInternal(std::string_view atomName, FileUpdateEvent evt);
            void setGLSWPath(bool clearExisting);

    AtomUniformPair loadSourceCode(const Str128& stageName,
                                   const Str8& extension,
                                   const string& header,
                                   size_t definesHash,
                                   bool reloadExisting,
                                   Str256& fileNameOut,
                                   eastl::string& sourceCodeOut) const;
private:
    static const string& ShaderFileRead(const ResourcePath& filePath, const ResourcePath& atomName, bool recurse, vector<ResourcePath>& foundAtoms, bool& wasParsed);
    static const string& ShaderFileReadLocked(const ResourcePath& filePath, const ResourcePath& atomName, bool recurse, vector<ResourcePath>& foundAtoms, bool& wasParsed);

    static bool ShaderFileRead(const ResourcePath& filePath, const ResourcePath& fileName, eastl::string& sourceCodeOut);
    static bool ShaderFileWrite(const ResourcePath& filePath, const ResourcePath& fileName, const char* sourceCode);

    static void DumpShaderTextCacheToDisk(const TextDumpEntry& entry);

    static eastl::string GatherUniformDeclarations(const eastl::string& source, vector<UniformDeclaration>& foundUniforms);

    static eastl::string PreprocessIncludes(const ResourcePath& name,
                                        const eastl::string& source,
                                        I32 level,
                                        vector<ResourcePath>& foundAtoms,
                                        bool lock);
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
    class ShaderProgramKernel {
      protected:
        static void UseShaderTextCache(const bool state) noexcept {
            ShaderProgram::UseShaderTextCache(state);
        }

        static void UseShaderBinaryCache(const bool state) noexcept {
            ShaderProgram::UseShaderBinaryCache(state);
        }

        friend class Kernel;
    };
}

};  // namespace Divide
#endif //_SHADER_PROGRAM_H_