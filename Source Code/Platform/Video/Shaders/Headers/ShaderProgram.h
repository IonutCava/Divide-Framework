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

#include "ShaderProgramFwd.h"
#include "ShaderDataUploader.h"

#include "Core/Resources/Headers/Resource.h"
#include "Platform/Video/Headers/GraphicsResource.h"
#include "Platform/Video/Headers/AttributeDescriptor.h"
#include "Platform/Video/Headers/DescriptorSets.h"

namespace FW {
    class FileWatcher;
};

namespace Divide {

class Kernel;
class Camera;
class Material;
class ResourceCache;

struct PushConstants;
struct Configuration;

enum class FileUpdateEvent : U8;

FWD_DECLARE_MANAGED_CLASS(ShaderProgram);

namespace TypeUtil {
    [[nodiscard]] const char* DescriptorSetUsageToString(DescriptorSetUsage setUsage) noexcept;
    [[nodiscard]] DescriptorSetUsage StringToDescriptorSetUsage(const string& name);
};



class ShaderModule : public GUIDWrapper, public GraphicsResource {
protected:
    using ShaderMap = ska::bytell_hash_map<U64, ShaderModule*>;

public:
    explicit ShaderModule(GFXDevice& context, const Str256& name);
    virtual ~ShaderModule();

    void AddRef() noexcept { _refCount.fetch_add(1); }
    /// Returns true if ref count reached 0
    size_t SubRef() noexcept { return _refCount.fetch_sub(1); }

    [[nodiscard]] size_t GetRef() const noexcept { return _refCount.load(); }

    PROPERTY_R(Str256, name);
    PROPERTY_R(bool, valid, false);

public:
    // ======================= static data ========================= //
    /// Remove a shader from the cache
    static void RemoveShader(ShaderModule* s, bool force = false);
    /// Returns a reference to an already loaded shader, null otherwise
    static ShaderModule* GetShader(const Str256& name);

    static void InitStaticData();
    static void DestroyStaticData();

protected:
    static ShaderModule* GetShaderLocked(const Str256& name);
    static void RemoveShaderLocked(ShaderModule* s, bool force = false);

protected:
    std::atomic_size_t _refCount;
    /// A list of preprocessor defines (if the bool in the pair is true, #define is automatically added
    vector<ModuleDefine> _definesList;

protected:
    /// Shader cache
    static ShaderMap s_shaderNameMap;
    static SharedMutex s_shaderNameLock;
};

class NOINITVTABLE ShaderProgram : public CachedResource,
                                   public GraphicsResource {
   public:
    static constexpr const char* UNIFORM_BLOCK_NAME = "dvd_uniforms";
    static constexpr U8 MAX_SLOTS_PER_DESCRIPTOR_SET = 16u;
    static constexpr U8 BONE_CRT_BUFFER_BINDING_SLOT = 12u;
    static constexpr U8 BONE_PREV_BUFFER_BINDING_SLOT = 13u;
    
    static U8 k_commandBufferID;

    // one per shader type!
    struct LoadData {
        enum class SourceCodeSource : U8 {
            SOURCE_FILES,
            TEXT_CACHE,
            SPIRV_CACHE,
            COUNT
        };
        enum class ShaderCacheType : U8 {
            GLSL,
            SPIRV,
            REFLECTION,
            COUNT
        };

        Reflection::UniformsSet _uniforms;
        std::vector<U32> _sourceCodeSpirV;
        eastl::string _sourceCodeGLSL;
        Str256 _name = "";
        Str256 _fileName = "";
        Str256 _sourceFile = "";
        size_t _definesHash = 0u;
        ShaderType _type = ShaderType::COUNT;
        string _uniformBlock{};
        Reflection::Data _reflectionData{};
        bool _compiled = false;
    };

    using ShaderLoadData = std::array<LoadData, to_base(ShaderType::COUNT)>;

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
                           ResourceCache& parentCache);

    virtual ~ShaderProgram();

    bool load() override;
    bool unload() override;

    inline bool recompile() {
        bool skipped = false;
        return recompile(skipped);
    }

    virtual bool recompile(bool& skipped);

    [[nodiscard]] bool uploadUniformData(const PushConstants& data, DescriptorSet& set, GFX::MemoryBarrierCommand& memCmdInOut);

    //==================== static methods ===============================//
    static void Idle(PlatformContext& platformContext);
    static void InitStaticData();
    static void DestroyStaticData();

    [[nodiscard]] static ErrorCode OnStartup(ResourceCache* parentCache);
    [[nodiscard]] static bool OnShutdown();
    [[nodiscard]] static bool OnThreadCreated(const GFXDevice& gfx, const std::thread::id& threadID);
                  static void OnEndFrame(GFXDevice& gfx);

    /// Queue a shaderProgram recompile request
    static bool RecompileShaderProgram(const Str256& name);
    /// Remove a shaderProgram from the program cache
    static bool UnregisterShaderProgram(ShaderProgramHandle shaderHandle);
    /// Add a shaderProgram to the program cache
    static void RegisterShaderProgram(ShaderProgram* shaderProgram);
    /// Find a specific shader program by handle.
    [[nodiscard]] static ShaderProgram* FindShaderProgram(ShaderProgramHandle shaderHandle);

    static void RebuildAllShaders();

    [[nodiscard]] static vector<ResourcePath> GetAllAtomLocations();

    [[nodiscard]] static I32 ShaderProgramCount() noexcept { return s_shaderCount.load(std::memory_order_relaxed); }

    [[nodiscard]] const ShaderProgramDescriptor& descriptor() const noexcept { return _descriptor; }

    [[nodiscard]] const char* getResourceTypeName() const noexcept override { return "ShaderProgram"; }

    static void OnAtomChange(std::string_view atomName, FileUpdateEvent evt);

    static [[nodiscard]] U8 GetGLBindingForDescriptorSlot(DescriptorSetUsage usage, U8 slot) noexcept;
    static [[nodiscard]] std::pair<DescriptorSetUsage, U8> GetDescriptorSlotForGLBinding(U8 binding, DescriptorSetBindingType type) noexcept;

    static void RegisterSetLayoutBinding(DescriptorSetUsage usage, U8 slot, DescriptorSetBindingType type, ShaderStageVisibility visibility);

    PROPERTY_R_IW();
    PROPERTY_RW(bool, highPriority, true);
    PROPERTY_R_IW(ShaderProgramHandle, handle, SHADER_INVALID_HANDLE);

    static Mutex g_cacheLock;

   protected:

     static bool SaveToCache(LoadData::ShaderCacheType cache, const LoadData& dataIn, const eastl::set<U64>& atomIDsIn);
     static bool LoadFromCache(LoadData::ShaderCacheType cache, LoadData& dataInOut, eastl::set<U64>& atomIDsOut);

   protected:
    /// Only 1 shader program per frame should be recompiled to avoid a lot of stuttering
    static ShaderQueue s_recompileQueue;
    /// Shader program cache
    static ShaderProgramMap s_shaderPrograms;

    struct LastRequestedShader {
        ShaderProgram* _program{ nullptr };
        ShaderProgramHandle _handle{ SHADER_INVALID_HANDLE };
    };
    static LastRequestedShader s_lastRequestedShaderProgram;
    static SharedMutex s_programLock;

protected:
    void threadedLoad(bool reloadExisting);
    virtual bool reloadShaders(hashMap<U64, PerFileShaderData>& fileData, bool reloadExisting);

    bool loadSourceCode(const ModuleDefines& defines,
                        bool reloadExisting,
                        LoadData& loadDataInOut,
                        Reflection::UniformsSet& previousUniformsInOut,
                        U8& blockIndexInOut);

    void loadAndParseGLSL(const ModuleDefines& defines,
                          bool reloadExisting,
                          LoadData& loadDataInOut,
                          Reflection::UniformsSet& previousUniformsInOut,
                          U8& blockIndexInOut,
                          eastl::set<U64>& atomIDsInOut);

    void initUniformUploader(const PerFileShaderData& loadData);

private:
    static const string& ShaderFileRead(const ResourcePath& filePath, const ResourcePath& atomName, bool recurse, eastl::set<U64>& foundAtomIDsInOut, bool& wasParsed);
    static const string& ShaderFileReadLocked(const ResourcePath& filePath, const ResourcePath& atomName, bool recurse, eastl::set<U64>& foundAtomIDsInOut, bool& wasParsed);

    static eastl::string PreprocessIncludes(const ResourcePath& name,
                                        const eastl::string& source,
                                        I32 level,
                                        eastl::set<U64>& foundAtomIDsInOut,
                                        bool lock);
   protected:
    template <typename T>
    friend class ImplResourceLoader;

    ResourceCache& _parentCache;
    const ShaderProgramDescriptor _descriptor;

   protected:
    vector<UniformBlockUploader> _uniformBlockBuffers;
    eastl::set<U64> _usedAtomIDs;

   protected:
    static std::atomic_int s_shaderCount;

    static I64 s_shaderFileWatcherID;

    /// Shaders loaded from files are kept as atoms
    static Mutex s_atomLock;
    static AtomMap s_atoms;
    static AtomInclusionMap s_atomIncludes;

    //extra entry for "common" location
    static ResourcePath shaderAtomLocationPrefix[to_base(ShaderType::COUNT) + 1];
    static Str8 shaderAtomExtensionName[to_base(ShaderType::COUNT) + 1];
    static U64 shaderAtomExtensionHash[to_base(ShaderType::COUNT) + 1];

    struct GLBindingsPerSet {
        U8 _glBinding{ INVALID_TEXTURE_BINDING };
        ShaderStageVisibility _visibility{ ShaderStageVisibility::COUNT };
        DescriptorSetBindingType _type{ DescriptorSetBindingType::COUNT };
    };
    using GLBindingsPerSetArray = std::array<GLBindingsPerSet, MAX_SLOTS_PER_DESCRIPTOR_SET>;

    static std::array<GLBindingsPerSetArray, to_base(DescriptorSetUsage::COUNT)> s_glBindingsPerSet;
};

struct PerFileShaderData {
    string _programName{};
    vector<ShaderModuleDescriptor> _modules;
    ShaderProgram::ShaderLoadData _loadData;
};

};  // namespace Divide
#endif //_SHADER_PROGRAM_H_