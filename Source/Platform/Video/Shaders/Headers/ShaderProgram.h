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
#ifndef DVD_SHADER_PROGRAM_H_
#define DVD_SHADER_PROGRAM_H_

#include "ShaderProgramFwd.h"
#include "ShaderDataUploader.h"

#include "Core/Resources/Headers/Resource.h"
#include "Platform/Video/Headers/GraphicsResource.h"
#include "Platform/Video/Headers/AttributeDescriptor.h"

namespace FW
{
    class FileWatcher;
};

namespace Divide
{

    class Kernel;
    class Camera;
    class Material;
    class ResourceCache;

    struct PushConstants;
    struct Configuration;

    enum class FileUpdateEvent : U8;

    FWD_DECLARE_MANAGED_CLASS( ShaderProgram );

    namespace TypeUtil
    {
        [[nodiscard]] const char* DescriptorSetUsageToString( DescriptorSetUsage setUsage ) noexcept;
        [[nodiscard]] DescriptorSetUsage StringToDescriptorSetUsage( const string& name );
    };

    FWD_DECLARE_MANAGED_CLASS( ShaderModule );

    class ShaderModule : public GUIDWrapper, public GraphicsResource
    {
    protected:
        using ShaderMap = hashMap<U64, ShaderModule_uptr>;

        // 10 seconds should be enough
        static constexpr U32 MAX_FRAME_LIFETIME = Config::TARGET_FRAME_RATE * 10;

    public:
        explicit ShaderModule( GFXDevice& context, const std::string_view name, U32 generation );
        virtual ~ShaderModule() override;

        void registerParent(ShaderProgram* parent);
        void deregisterParent(ShaderProgram* parent);

        PROPERTY_R( Str<256>, name );
        PROPERTY_R( bool, valid, false );
        PROPERTY_R( bool, inUse, true );
        PROPERTY_R( U32, generation, 0u);
        PROPERTY_R( U64, lastUsedFrame, U64_MAX - MAX_FRAME_LIFETIME - 1u);

    
    public:
        // ======================= static data ========================= //
        /// Returns a reference to an already loaded shader, null otherwise
        static ShaderModule* GetShader( const std::string_view name );

        static void Idle(bool fast);
        static void InitStaticData();
        static void DestroyStaticData();

    protected:
        static ShaderModule* GetShaderLocked( const std::string_view name );

    protected:
        Mutex _parentLock;
        eastl::fixed_vector<ShaderProgram*, 4, true> _parents;

    protected:
        /// Shader cache
        static std::atomic_bool s_modulesRemoved;
        static ShaderMap s_shaderNameMap;
        static SharedMutex s_shaderNameLock;
    };

    class NOINITVTABLE ShaderProgram : public CachedResource,
        public GraphicsResource
    {
    public:
        static constexpr const char* UNIFORM_BLOCK_NAME = "dvd_uniforms";

        static U8 k_commandBufferID;

        // one per shader type!
        struct LoadData
        {
            enum class SourceCodeSource : U8
            {
                SOURCE_FILES,
                TEXT_CACHE,
                SPIRV_CACHE,
                COUNT
            };
            enum class ShaderCacheType : U8
            {
                GLSL,
                SPIRV,
                REFLECTION,
                COUNT
            };

            Reflection::UniformsSet _uniforms;
            std::vector<U32> _sourceCodeSpirV;
            string _sourceCodeGLSL;
            Str<256> _sourceName{};
            Str<256> _shaderName{};
            Str<256> _sourceFile{};
            size_t _definesHash{ 0u };
            ShaderType _type{ ShaderType::COUNT };
            string _uniformBlock{};
            Reflection::Data _reflectionData{};
            bool _compiled{ false };
        };

        struct ShaderQueueEntry
        {
            ShaderProgram* _program{nullptr};
            U32 _queueDelay{0u};
            U32 _queueDelayHighWaterMark{1u};
        };

        using RenderTargets = std::array<bool, to_base( RTColourAttachmentSlot::COUNT )>;
        using ShaderLoadData = std::array<LoadData, to_base( ShaderType::COUNT )>;

        using ShaderProgramMap = std::array<ShaderProgramMapEntry, U16_MAX>;

        using AtomMap = hashMap<U64 /*name hash*/, string>;
        using AtomInclusionMap = hashMap<U64 /*name hash*/, eastl::set<U64>>;
        using ShaderQueue = eastl::stack<ShaderQueueEntry, vector_fast<ShaderQueueEntry>>;

        struct BindingsPerSet
        {
            BaseType<ShaderStageVisibility> _visibility{ to_base( ShaderStageVisibility::NONE ) };
            DescriptorSetBindingType _type{ DescriptorSetBindingType::COUNT };
            U8 _glBinding{ INVALID_TEXTURE_BINDING };
        };
        using BindingsPerSetArray = std::array<BindingsPerSet, MAX_BINDINGS_PER_DESCRIPTOR_SET>;
        using SetUsageData = std::array<bool, to_base(DescriptorSetUsage::COUNT)>;
        using BindingSetData = std::array<BindingsPerSetArray, to_base( DescriptorSetUsage::COUNT )>;

    public:
        explicit ShaderProgram( GFXDevice& context,
                                size_t descriptorHash,
                                std::string_view shaderName,
                                std::string_view shaderFileName,
                                const ResourcePath& shaderFileLocation,
                                ShaderProgramDescriptor descriptor,
                                ResourceCache& parentCache );

        ~ShaderProgram() override;

        bool load() override;
        bool unload() override;

        inline bool recompile()
        {
            bool skipped = false;
            return recompile( skipped );
        }

        bool recompile( bool& skipped );

        virtual ShaderResult validatePreBind( bool rebind = true );

        [[nodiscard]] bool uploadUniformData( const PushConstants& data, DescriptorSet& set, GFX::MemoryBarrierCommand& memCmdInOut );

        //==================== static methods ===============================//
        static void Idle( PlatformContext& platformContext, bool fast );
        static void InitStaticData();
        static void DestroyStaticData();

        [[nodiscard]] static ErrorCode OnStartup( ResourceCache* parentCache );
        [[nodiscard]] static bool OnShutdown();
        [[nodiscard]] static bool OnThreadCreated( const GFXDevice& gfx, const std::thread::id& threadID );
        static void OnBeginFrame( GFXDevice& gfx );
        static void OnEndFrame( GFXDevice& gfx );

        /// Queue a shaderProgram recompile request
        static bool RecompileShaderProgram( const std::string_view name );
        /// Remove a shaderProgram from the program cache
        static bool UnregisterShaderProgram( ShaderProgramHandle shaderHandle );
        /// Add a shaderProgram to the program cache
        static void RegisterShaderProgram( ShaderProgram* shaderProgram );
        /// Find a specific shader program by handle.
        [[nodiscard]] static ShaderProgram* FindShaderProgram( ShaderProgramHandle shaderHandle );

        static void RebuildAllShaders();

        [[nodiscard]] static vector<ResourcePath> GetAllAtomLocations();

        [[nodiscard]] static I32 ShaderProgramCount() noexcept
        {
            return s_shaderCount.load( std::memory_order_relaxed );
        }

        [[nodiscard]] const ShaderProgramDescriptor& descriptor() const noexcept
        {
            return _descriptor;
        }

        [[nodiscard]] const char* getResourceTypeName() const noexcept override
        {
            return "ShaderProgram";
        }

        static void OnAtomChange( std::string_view atomName, FileUpdateEvent evt );

        [[nodiscard]] static U8 GetGLBindingForDescriptorSlot( DescriptorSetUsage usage, U8 slot ) noexcept;
        [[nodiscard]] static std::pair<DescriptorSetUsage, U8> GetDescriptorSlotForGLBinding( U8 binding, DescriptorSetBindingType type ) noexcept;

        [[nodiscard]] static BindingSetData& GetBindingSetData() noexcept;

        static void RegisterSetLayoutBinding( DescriptorSetUsage usage, U8 slot, DescriptorSetBindingType type, ShaderStageVisibility visibility );
        static ErrorCode SubmitSetLayouts(GFXDevice& gfx);
        static U32  GetBindingCount( DescriptorSetUsage usage, DescriptorSetBindingType type);

        PROPERTY_RW( bool, highPriority, true );
        PROPERTY_RW( bool, useShaderCache, true );
        PROPERTY_R_IW( ShaderProgramHandle, handle, SHADER_INVALID_HANDLE );
        PROPERTY_R_IW( BindingsPerSetArray, perDrawDescriptorSetLayout );
        PROPERTY_R_IW( RenderTargets, fragmentOutputs );
        PROPERTY_R_IW( SetUsageData, setUsage );

        static Mutex g_cacheLock;

    protected:

        static bool SaveToCache( LoadData::ShaderCacheType cache, const LoadData& dataIn, const eastl::set<U64>& atomIDsIn );
        static bool LoadFromCache( LoadData::ShaderCacheType cache, LoadData& dataInOut, eastl::set<U64>& atomIDsOut );

    protected:
        static ShaderQueue s_recompileQueue;
        static ShaderQueue s_recompileFailedQueue;
        /// Shader program cache
        static ShaderProgramMap s_shaderPrograms;
        static eastl::fixed_vector<ShaderProgram*, U16_MAX, false> s_usedShaderPrograms;

        struct LastRequestedShader
        {
            ShaderProgram* _program{ nullptr };
            ShaderProgramHandle _handle{ SHADER_INVALID_HANDLE };
        };
        static LastRequestedShader s_lastRequestedShaderProgram;
        static SharedMutex s_programLock;

    protected:
        virtual bool loadInternal( hashMap<U64, PerFileShaderData>& fileData, bool overwrite );

        bool loadSourceCode( const ModuleDefines& defines,
                             bool reloadExisting,
                             LoadData& loadDataInOut,
                             Reflection::UniformsSet& previousUniformsInOut,
                             U8& blockIndexInOut );

        void loadAndParseGLSL( const ModuleDefines& defines,
                               LoadData& loadDataInOut,
                               Reflection::UniformsSet& previousUniformsInOut,
                               U8& blockIndexInOut,
                               eastl::set<U64>& atomIDsInOut );

        void initDrawDescriptorSetLayout( const PerFileShaderData& loadData );
        void initUniformUploader( const PerFileShaderData& loadData );


    private:
        static void EraseAtom(const U64 atomHash);
        static void EraseAtomLocked(const U64 atomHash);

        static const string& ShaderFileRead( const ResourcePath& filePath, std::string_view atomName, bool recurse, eastl::set<U64>& foundAtomIDsInOut, bool& wasParsed );
        static const string& ShaderFileReadLocked( const ResourcePath& filePath, std::string_view atomName, bool recurse, eastl::set<U64>& foundAtomIDsInOut, bool& wasParsed );

        static void PreprocessIncludes( std::string_view name,
                                        string& sourceInOut,
                                        I32 level,
                                        eastl::set<U64>& foundAtomIDsInOut,
                                        bool lock );
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
        static ResourcePath shaderAtomLocationPrefix[to_base( ShaderType::COUNT ) + 1];
        static Str<8> shaderAtomExtensionName[to_base( ShaderType::COUNT ) + 1];
        static U64 shaderAtomExtensionHash[to_base( ShaderType::COUNT ) + 1];

        static BindingSetData s_bindingsPerSet;
    };

    struct PerFileShaderData
    {
        string _programName{};
        vector<ShaderModuleDescriptor> _modules;
        ShaderProgram::ShaderLoadData _loadData;
    };

};  // namespace Divide

#endif //DVD_SHADER_PROGRAM_H_
