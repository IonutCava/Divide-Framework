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

#include "ShaderDataUploader.h"

#include "Core/Headers/ObjectPool.h"
#include "Core/Resources/Headers/Resource.h"
#include "Core/Resources/Headers/ResourceDescriptor.h"
#include "Platform/Video/Headers/GraphicsResource.h"
#include "Platform/Video/Headers/RenderAPIEnums.h"
#include "Platform/Video/Headers/AttributeDescriptor.h"

namespace FW {
    class FileWatcher;
};

namespace Divide {

class Kernel;
class Camera;
class Material;
class ResourceCache;
class ShaderProgramDescriptor;

struct PushConstants;
struct Configuration;

enum class FileUpdateEvent : U8;

FWD_DECLARE_MANAGED_CLASS(ShaderProgram);

namespace TypeUtil {
    const char* DescriptorSetUsageToString(DescriptorSetUsage setUsage) noexcept;
    DescriptorSetUsage StringToDescriptorSetUsage(const string& name);

    const char* ShaderBufferLocationToString(ShaderBufferLocation bufferLocation) noexcept;
    ShaderBufferLocation StringToShaderBufferLocation(const string& name);
};

struct ModuleDefine {
    ModuleDefine() = default;
    ModuleDefine(const char* define, const bool addPrefix = true) : ModuleDefine(string{ define }, addPrefix) {}
    ModuleDefine(const string& define, const bool addPrefix = true) : _define(define), _addPrefix(addPrefix) {}

    string _define;
    bool _addPrefix = true;
};

using ModuleDefines = vector<ModuleDefine>;

struct ShaderModuleDescriptor {
    ShaderModuleDescriptor() = default;
    explicit ShaderModuleDescriptor(ShaderType type, const Str64& file, const Str64& variant = "")
        : _moduleType(type), _sourceFile(file), _variant(variant)
    {
    }

    ModuleDefines _defines;
    Str64 _sourceFile;
    Str64 _variant;
    ShaderType _moduleType = ShaderType::COUNT;
};

struct PerFileShaderData;
class ShaderProgramDescriptor final : public PropertyDescriptor {
public:
    ShaderProgramDescriptor() noexcept
        : PropertyDescriptor(DescriptorType::DESCRIPTOR_SHADER) 
    {
    }

    size_t getHash() const override;
    Str256 _name;
    ModuleDefines _globalDefines;
    vector<ShaderModuleDescriptor> _modules;
    PrimitiveTopology _primitiveTopology = PrimitiveTopology::COUNT;
    AttributeMap _vertexFormat;
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
   public:
    static constexpr char* UNIFORM_BLOCK_NAME = "dvd_uniforms";

    // one per shader type!
    struct LoadData {
        enum class SourceCodeSource : U8 {
            SOURCE_FILES,
            TEXT_CACHE,
            SPIRV_CACHE,
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
        SourceCodeSource _codeSource = SourceCodeSource::COUNT;
        string _uniformBlock{};
        Reflection::Data _reflectionData{};
        bool _compiled = false;
    };

    using ShaderLoadData = vector<LoadData>;

    using Handle = PoolHandle;
    static constexpr Handle INVALID_HANDLE{ U16_MAX, U8_MAX };

    static bool s_UseBindlessTextures;

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

    void uploadPushConstants(const PushConstants& constants, GFX::MemoryBarrierCommand& memCmdInOut);
    void preparePushConstants();

    //==================== static methods ===============================//
    static void Idle(PlatformContext& platformContext);
    [[nodiscard]] static ErrorCode OnStartup(ResourceCache* parentCache);
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

    static void RebuildAllShaders();

    [[nodiscard]] static vector<ResourcePath> GetAllAtomLocations();

    [[nodiscard]] static I32 ShaderProgramCount() noexcept { return s_shaderCount.load(std::memory_order_relaxed); }

    [[nodiscard]] const ShaderProgramDescriptor& descriptor() const noexcept { return _descriptor; }

    [[nodiscard]] const char* getResourceTypeName() const noexcept override { return "ShaderProgram"; }

    static void OnAtomChange(std::string_view atomName, FileUpdateEvent evt);

    PROPERTY_RW(bool, highPriority, true);
    PROPERTY_R_IW(Handle, handle, INVALID_HANDLE);
    PROPERTY_R_IW(size_t, vertexFormatHash, 0u);

   protected:

     static bool GLSLToSPIRV(LoadData& dataInOut, bool targetVulkan, const eastl::set<U64>& atomIDsIn);

     static bool SaveSPIRVToCache(const LoadData& dataIn, bool targetVulkan, const eastl::set<U64>& atomIDsIn);
     static bool LoadSPIRVFromCache(LoadData& dataInOut, bool targetVulkan, eastl::set<U64>& atomIDsOut);

     static bool SaveTextToCache(const LoadData& dataIn, bool targetVulkan, const eastl::set<U64>& atomIDsIn);
     static bool LoadTextFromCache(LoadData& dataInOut, bool targetVulkan, eastl::set<U64>& atomIDsOut);

   protected:
    /// Only 1 shader program per frame should be recompiled to avoid a lot of stuttering
    static ShaderQueue s_recompileQueue;
    /// Shader program cache
    static ShaderProgramMap s_shaderPrograms;

    struct LastRequestedShader {
        ShaderProgram* _program = nullptr;
        Handle _handle = INVALID_HANDLE;
    };
    static LastRequestedShader s_lastRequestedShaderProgram;
    static SharedMutex s_programLock;

protected:
    virtual void threadedLoad(bool reloadExisting);
    virtual bool reloadShaders(hashMap<U64, PerFileShaderData>& fileData, bool reloadExisting);

    bool loadSourceCode(const ModuleDefines& defines,
                        bool reloadExisting,
                        LoadData& loadDataInOut,
                        Reflection::UniformsSet& previousUniformsInOut,
                        U32& blockIndexInOut);

    void loadAndParseGLSL(const ModuleDefines& defines,
                          bool reloadExisting,
                          LoadData& loadDataInOut,
                          Reflection::UniformsSet& previousUniformsInOut,
                          U32& blockIndexInOut,
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
    static Mutex g_textDumpLock;
    static Mutex g_binaryDumpLock;
    static AtomMap s_atoms;
    static AtomInclusionMap s_atomIncludes;

    //extra entry for "common" location
    static ResourcePath shaderAtomLocationPrefix[to_base(ShaderType::COUNT) + 1];
    static Str8 shaderAtomExtensionName[to_base(ShaderType::COUNT) + 1];
    static U64 shaderAtomExtensionHash[to_base(ShaderType::COUNT) + 1];
};

struct PerFileShaderData {
    string _programName{};
    vector<ShaderModuleDescriptor> _modules;
    ShaderProgram::ShaderLoadData _loadData;
};

};  // namespace Divide
#endif //_SHADER_PROGRAM_H_