

#include "Headers/ShaderProgram.h"
#include "Headers/GLSLToSPIRV.h"

#include "Managers/Headers/ProjectManager.h"

#include "Rendering/Headers/Renderer.h"
#include "Geometry/Material/Headers/Material.h"
#include "Scenes/Headers/SceneShaderData.h"
#include "Scenes/Headers/SceneEnvironmentProbePool.h"

#include "Core/Headers/Configuration.h"
#include "Core/Headers/PlatformContext.h"
#include "Core/Resources/Headers/ResourceCache.h"

#include "Platform/Video/Headers/GFXDevice.h"
#include "Platform/Video/Shaders/glsw/Headers/glsw.h"

#include "Platform/File/Headers/FileManagement.h"
#include "Platform/File/Headers/FileUpdateMonitor.h"
#include "Platform/File/Headers/FileWatcherManager.h"

extern "C"
{
    #include <cppdef.h>
    #include <fpp.h>
}

#include <vulkan/vulkan.hpp>

namespace Divide
{

    namespace TypeUtil
    {
        const char* DescriptorSetUsageToString( const DescriptorSetUsage setUsage ) noexcept
        {
            return Names::descriptorSetUsage[to_base( setUsage )];
        }

        DescriptorSetUsage StringToDescriptorSetUsage( const string& name )
        {
            for ( U8 i = 0; i < to_U8( DescriptorSetUsage::COUNT ); ++i )
            {
                if ( strcmp( name.c_str(), Names::descriptorSetUsage[i] ) == 0 )
                {
                    return static_cast<DescriptorSetUsage>(i);
                }
            }

            return DescriptorSetUsage::COUNT;
        }
    };

    constexpr I8 s_maxHeaderRecursionLevel = 64;
    
    NO_DESTROY Mutex ShaderProgram::s_atomLock;
    NO_DESTROY Mutex ShaderProgram::g_cacheLock;
    NO_DESTROY ShaderProgram::AtomMap ShaderProgram::s_atoms;
    NO_DESTROY ShaderProgram::AtomInclusionMap ShaderProgram::s_atomIncludes;

    I64 ShaderProgram::s_shaderFileWatcherID = -1;
    NO_DESTROY ResourcePath ShaderProgram::shaderAtomLocationPrefix[to_base( ShaderType::COUNT ) + 1];
    U64 ShaderProgram::shaderAtomExtensionHash[to_base( ShaderType::COUNT ) + 1];
    NO_DESTROY Str<8> ShaderProgram::shaderAtomExtensionName[to_base( ShaderType::COUNT ) + 1];

    NO_DESTROY ShaderProgram::ShaderQueue ShaderProgram::s_recompileQueue;
    NO_DESTROY ShaderProgram::ShaderQueue ShaderProgram::s_recompileFailedQueue;
    ShaderProgram::ShaderProgramMap ShaderProgram::s_shaderPrograms;
    fixed_vector<ShaderProgram*, U16_MAX> ShaderProgram::s_usedShaderPrograms;
    ShaderProgram::LastRequestedShader ShaderProgram::s_lastRequestedShaderProgram = {};
    U8 ShaderProgram::k_commandBufferID = U8_MAX - MAX_BINDINGS_PER_DESCRIPTOR_SET;

    SharedMutex ShaderProgram::s_programLock;
    std::atomic_int ShaderProgram::s_shaderCount;

    ShaderProgram::BindingSetData ShaderProgram::s_bindingsPerSet;

    NO_DESTROY static UpdateListener g_sFileWatcherListener(
        []( const std::string_view atomName, const FileUpdateEvent evt )
        {
            ShaderProgram::OnAtomChange( atomName, evt );
        }
    );

    namespace Preprocessor
    {
        struct WorkData
        {
            string _input;
            std::string_view _fileName;

            std::array<char, 16 << 10> _scratch{};
            string _depends = "";
            string _default = "";
            string _output = "";

            U32 _scratchPos = 0u;
            U32 _fGetsPos = 0u;
            bool _firstError = true;
        };

        constexpr U8 g_maxTagCount = 64;

        NO_DESTROY thread_local static WorkData g_workData;
        NO_DESTROY thread_local static fppTag g_tags[g_maxTagCount]{};
        NO_DESTROY thread_local static fppTag* g_tagHead = g_tags;

        namespace Callback
        {
            FORCE_INLINE void AddDependency( const char* file, void* userData )
            {
                static_cast<WorkData*>(userData)->_depends.append(Util::StringFormat("\n{}", file));
            }

            static char* Input( char* buffer, const int size, void* userData ) noexcept
            {
                WorkData* work = static_cast<WorkData*>(userData);
                int i = 0;
                for ( char ch = work->_input[work->_fGetsPos];
                      work->_fGetsPos < work->_input.size() && i < size - 1; ch = work->_input[++work->_fGetsPos] )
                {
                    buffer[i++] = ch;

                    if ( ch == '\n' || i == size )
                    {
                        buffer[i] = '\0';
                        work->_fGetsPos++;
                        return buffer;
                    }
                }

                return nullptr;
            }

            FORCE_INLINE void Output( const int ch, void* userData )
            {
                static_cast<WorkData*>(userData)->_output += static_cast<char>(ch);
            }

            static char* Scratch( const std::string_view fileName )
            {
                char* result = &g_workData._scratch[g_workData._scratchPos];
                strncpy( result, fileName.data(),fileName.size() );
                g_workData._scratchPos += to_U32( fileName.size() ) + 1;

                return result;
            }

            static void Error( void* userData, const char* format, va_list args )
            {
                WorkData* work = static_cast<WorkData*>(userData);

                string message;
                const size_t length = to_size(vsnprintf( nullptr, 0, format, args ) + 1);
                message.resize( length );
                vsnprintf( message.data(), length, format, args );
                
                if ( work->_firstError )
                {
                    work->_firstError = false;
                    Console::errorfn( "------------------------------------------" );
                    Console::errorfn( LOCALE_STR( "ERROR_GLSL_PARSE_ERROR_NAME_SHORT" ), work->_fileName );
                }

                if ( !message.empty() )
                {
                    Console::errorfn( LOCALE_STR( "ERROR_GLSL_PARSE_ERROR_MSG" ), message );
                }
                else
                {
                    Console::errorfn( "------------------------------------------\n" );
                }
            }
        }

        static void OnThreadCreated()
        {
            const auto setTag = []( const int tag, void* value )
            {
                g_tagHead->tag = tag;
                g_tagHead->data = value;
                ++g_tagHead;
            };

            const auto setFlag = []( const int tag, bool flag )
            {
                static bool data = true;

                g_tagHead->tag = tag;
                g_tagHead->data = (flag ? &data : nullptr);
                ++g_tagHead;
            };

            setFlag( FPPTAG_KEEPCOMMENTS, true );
            setFlag( FPPTAG_IGNORE_NONFATAL, false );
            setFlag( FPPTAG_IGNORE_CPLUSPLUS, false );
            setFlag( FPPTAG_LINE, false );
            setFlag( FPPTAG_WARNILLEGALCPP, false );
            setFlag( FPPTAG_OUTPUTLINE, false );
            setFlag( FPPTAG_IGNOREVERSION, false );
            setFlag( FPPTAG_OUTPUTINCLUDES, false );
            setFlag( FPPTAG_OUTPUTBALANCE, true );
            setFlag( FPPTAG_OUTPUTSPACE, true );
            setFlag( FPPTAG_NESTED_COMMENTS, true );
            setFlag( FPPTAG_WARN_NESTED_COMMENTS, false );
            setFlag( FPPTAG_WARNMISSINCLUDE, false );
            setFlag( FPPTAG_RIGHTCONCAT, true );
            setFlag( FPPTAG_DISPLAYFUNCTIONS, false );
            setFlag( FPPTAG_WEBMODE, false );

            setTag( FPPTAG_DEPENDS, (void*)Callback::AddDependency );
            setTag( FPPTAG_INPUT, (void*)Callback::Input );
            setTag( FPPTAG_OUTPUT, (void*)Callback::Output );
            setTag( FPPTAG_ERROR, (void*)Callback::Error );

            setTag( FPPTAG_USERDATA, &g_workData );

        }

        static bool PreProcessMacros( const std::string_view fileName, string& sourceInOut )
        {
            if ( sourceInOut.empty() )
            {
                return false;
            }

            g_workData = {};
            g_workData._input = sourceInOut;
            g_workData._fileName = fileName;
            std::erase( g_workData._input, '\r' );

            fppTag* tagptr = g_tagHead;

            tagptr->tag = FPPTAG_INPUT_NAME;
            tagptr->data = (void*)Callback::Scratch( fileName );
            ++tagptr;

            tagptr->tag = FPPTAG_END;
            tagptr->data = nullptr;
            ++tagptr;

            bool ret = false;
            if ( fppPreProcess( g_tags ) == 0 )
            {
                ret = true;
                sourceInOut = g_workData._output;
            }

            return ret;
        }

    } //Preprocessor

    namespace
    {
        U64 s_newestShaderAtomWriteTime = 0u; ///< Used to detect modified shader atoms to validate/invalidate shader cache
        bool s_useShaderCache = true;
        bool s_targetOpenGL = false;

        U8 s_textureSlot = 0u;
        U8 s_imageSlot   = 0u;
        U8 s_bufferSlot =  0u;
        U8 s_computeBufferSlot =  0u;

        constexpr U8 s_uniformsStartOffset = 14u;

        void RefreshBindingSlots()
        {
            s_textureSlot = 0u;
            s_imageSlot   = 0u;
            s_bufferSlot  = 0u;
            s_computeBufferSlot  = 0u;
        }

        [[nodiscard]] ResourcePath ShaderAPILocation()
        {
            return (s_targetOpenGL ? Paths::Shaders::g_cacheLocationGL : Paths::Shaders::g_cacheLocationVK);
        }

        [[nodiscard]] ResourcePath ShaderBuildCacheLocation()
        {
            return Paths::Shaders::g_cacheLocation / Paths::g_buildTypeLocation;
        }

        [[nodiscard]] ResourcePath ShaderParentCacheLocation()
        {
            return ShaderBuildCacheLocation() / ShaderAPILocation();
        }

        [[nodiscard]] ResourcePath SpvCacheLocation()
        {
            return ShaderParentCacheLocation() / Paths::Shaders::g_cacheLocationSpv;
        }

        [[nodiscard]] ResourcePath ReflCacheLocation()
        {
            return ShaderParentCacheLocation() / Paths::Shaders::g_cacheLocationRefl;
        }

        [[nodiscard]] ResourcePath TxtCacheLocation()
        {
            return ShaderParentCacheLocation() / Paths::Shaders::g_cacheLocationText;
        }

        [[nodiscard]] ResourcePath SpvTargetName( const Str<256>& fileName )
        {
            return ResourcePath{ fileName + "." + Paths::Shaders::g_SPIRVExt.c_str() };
        }

        [[nodiscard]] ResourcePath ReflTargetName( const Str<256>& fileName )
        {
            return ResourcePath { fileName + "." + Paths::Shaders::g_ReflectionExt.c_str() };
        }

        [[nodiscard]] bool ValidateCacheLocked( const ShaderProgram::LoadData::ShaderCacheType type, const Str<256>& sourceFileName, const Str<256>& fileName )
        {
            if ( !s_useShaderCache )
            {
                return false;
            }

            //"There are only two hard things in Computer Science: cache invalidation and naming things" - Phil Karlton
            //"There are two hard things in computer science: cache invalidation, naming things, and off-by-one errors." - Leon Bambrick

            // Get our source file's "last written" timestamp. Every cache file that's older than this is automatically out-of-date
            U64 lastWriteTime = 0u, lastWriteTimeCache = 0u;
            const ResourcePath sourceShaderFullPath = Paths::Shaders::GLSL::g_GLSLShaderLoc / sourceFileName;
            if ( fileLastWriteTime( sourceShaderFullPath, lastWriteTime ) != FileError::NONE )
            {
                return false;
            }

            ResourcePath filePath;
            switch ( type )
            {
                case ShaderProgram::LoadData::ShaderCacheType::REFLECTION: filePath = ReflCacheLocation() / ReflTargetName( fileName ); break;
                case ShaderProgram::LoadData::ShaderCacheType::GLSL: filePath = TxtCacheLocation() / fileName; break;
                case ShaderProgram::LoadData::ShaderCacheType::SPIRV: filePath = SpvCacheLocation() / SpvTargetName( fileName ); break;

                default:
                case ShaderProgram::LoadData::ShaderCacheType::COUNT: return false;
            }

            if ( fileLastWriteTime( filePath, lastWriteTimeCache ) != FileError::NONE ||
                 lastWriteTimeCache < lastWriteTime ||
                 lastWriteTimeCache < s_newestShaderAtomWriteTime )
            {
                return false;
            }

            return true;
        }

        [[nodiscard]] bool DeleteCacheLocked( const ShaderProgram::LoadData::ShaderCacheType type, const Str<256>& fileName )
        {
            FileError err = FileError::NONE;
            switch ( type )
            {
                case ShaderProgram::LoadData::ShaderCacheType::REFLECTION: err = deleteFile( ReflCacheLocation(), ReflTargetName( fileName ).string() ); break;
                case ShaderProgram::LoadData::ShaderCacheType::GLSL: err = deleteFile( TxtCacheLocation(), fileName.c_str() ); break;
                case ShaderProgram::LoadData::ShaderCacheType::SPIRV: err = deleteFile( SpvCacheLocation(), SpvTargetName( fileName ).string() ); break;

                default:
                case ShaderProgram::LoadData::ShaderCacheType::COUNT: err = FileError::FILE_EMPTY; return false;
            }

            return err == FileError::NONE || err == FileError::FILE_NOT_FOUND;
        }

        [[nodiscard]] bool DeleteCache( const ShaderProgram::LoadData::ShaderCacheType type, const Str<256>& fileName )
        {
            LockGuard<Mutex> rw_lock( ShaderProgram::g_cacheLock );
            if ( type == ShaderProgram::LoadData::ShaderCacheType::COUNT )
            {
                bool ret = false;
                ret = DeleteCacheLocked( ShaderProgram::LoadData::ShaderCacheType::REFLECTION, fileName ) || ret;
                ret = DeleteCacheLocked( ShaderProgram::LoadData::ShaderCacheType::GLSL, fileName ) || ret;
                ret = DeleteCacheLocked( ShaderProgram::LoadData::ShaderCacheType::SPIRV, fileName ) || ret;
                return ret;

            }

            return DeleteCacheLocked( type, fileName );
        }
    };

    static bool InitGLSW( const RenderAPI renderingAPI, const Configuration& config)
    {
        const auto AppendToShaderHeader = []( const ShaderType type, const string& entry )
        {
            glswAddDirectiveToken( type != ShaderType::COUNT ? Names::shaderTypes[to_U8( type )] : "", entry.c_str() );
        };

        const auto AppendResourceBindingSlots = [&AppendToShaderHeader]( const bool targetOpenGL )
        {

            if ( targetOpenGL )
            {
                const auto ApplyDescriptorSetDefines = [&AppendToShaderHeader]( const DescriptorSetUsage setUsage )
                {
                    for ( U8 i = 0u; i < MAX_BINDINGS_PER_DESCRIPTOR_SET; ++i )
                    {
                        const U8 glSlot = ShaderProgram::GetGLBindingForDescriptorSlot( setUsage, i );
                        AppendToShaderHeader( ShaderType::COUNT, Util::StringFormat( "#define {}_{} {}",
                                                                                     TypeUtil::DescriptorSetUsageToString( setUsage ),
                                                                                     i,
                                                                                     glSlot ).c_str() );
                    }
                };
                ApplyDescriptorSetDefines( DescriptorSetUsage::PER_DRAW );
                ApplyDescriptorSetDefines( DescriptorSetUsage::PER_BATCH );
                ApplyDescriptorSetDefines( DescriptorSetUsage::PER_PASS );
                ApplyDescriptorSetDefines( DescriptorSetUsage::PER_FRAME );

                AppendToShaderHeader( ShaderType::COUNT, "#define DESCRIPTOR_SET_RESOURCE(SET, BINDING) layout(binding = CONCATENATE(SET, BINDING))" );
                AppendToShaderHeader( ShaderType::COUNT, "#define DESCRIPTOR_SET_RESOURCE_OFFSET(SET, BINDING, OFFSET) layout(binding = CONCATENATE(SET, BINDING), offset = OFFSET)" );
                AppendToShaderHeader( ShaderType::COUNT, "#define DESCRIPTOR_SET_RESOURCE_LAYOUT(SET, BINDING, LAYOUT) layout(binding = CONCATENATE(SET, BINDING), LAYOUT)" );
                AppendToShaderHeader( ShaderType::COUNT, "#define DESCRIPTOR_SET_RESOURCE_OFFSET_LAYOUT(SET, BINDING, OFFSET, LAYOUT) layout(binding = CONCATENATE(SET, BINDING), offset = OFFSET, LAYOUT)" );
            }
            else
            {
                for ( U8 i = 0u; i < to_base( DescriptorSetUsage::COUNT ); ++i )
                {
                    AppendToShaderHeader( ShaderType::COUNT, Util::StringFormat( "#define {} {}", TypeUtil::DescriptorSetUsageToString( static_cast<DescriptorSetUsage>(i) ), i ).c_str() );
                }
                AppendToShaderHeader( ShaderType::COUNT, "#define DESCRIPTOR_SET_RESOURCE(SET, BINDING) layout(set = SET, binding = BINDING)" );
                AppendToShaderHeader( ShaderType::COUNT, "#define DESCRIPTOR_SET_RESOURCE_OFFSET(SET, BINDING, OFFSET) layout(set = SET, binding = BINDING, offset = OFFSET)" );
                AppendToShaderHeader( ShaderType::COUNT, "#define DESCRIPTOR_SET_RESOURCE_LAYOUT(SET, BINDING, LAYOUT) layout(set = SET, binding = BINDING, LAYOUT)" );
                AppendToShaderHeader( ShaderType::COUNT, "#define DESCRIPTOR_SET_RESOURCE_OFFSET_LAYOUT(SET, BINDING, OFFSET, LAYOUT) layout(set = SET, binding = BINDING, offset = OFFSET, LAYOUT)" );
            }
        };

        constexpr std::pair<const char*, const char*> shaderVaryings[] =
        {
            { "vec4"       , "_vertexW"},        
            { "vec4"       , "_vertexWV"},       
            { "vec3"       , "_normalWV"},       
            { "vec2"       , "_texCoord"},       
            { "flat uvec4" , "_indirectionIDs"}, 
            { "flat uint"  , "_LoDLevel"},       
            { "flat uint"  , "_SelectionFlag"},       
        };

        constexpr const char* crossTypeGLSLHLSL = "#define float2 vec2\n"
            "#define float3 vec3\n"
            "#define float4 vec4\n"
            "#define int2 ivec2\n"
            "#define int3 ivec3\n"
            "#define int4 ivec4\n"
            "#define float2x2 mat2\n"
            "#define float3x3 mat3\n"
            "#define float4x4 mat4\n"
            "#define lerp mix";

        const auto getPassData = [&]( const ShaderType type ) -> string
        {
            string baseString = "     _out.{} = _in[index].{};";
            if ( type == ShaderType::TESSELLATION_CTRL )
            {
                baseString = "    _out[gl_InvocationID].{} = _in[index].{};";
            }

            string passData( "void PassData(in int index) {" );
            passData.append( "\n" );
            for ( const auto& [varType, name] : shaderVaryings )
            {
                passData.append( Util::StringFormat( baseString, name, name ) );
                passData.append( "\n" );
            }

            passData.append( "#if defined(HAS_VELOCITY)\n" );
            passData.append( Util::StringFormat( baseString, "_prevVertexWVP", "_prevVertexWVP" ) );
            passData.append( "\n#endif //HAS_VELOCITY\n" );

            passData.append( "#if defined(ENABLE_TBN)\n" );
            passData.append( Util::StringFormat( baseString, "_tbnWV", "_tbnWV" ) );
            passData.append( "\n#endif //ENABLE_TBN\n" );

            passData.append( "}\n" );

            return passData;
        };

        const auto addVaryings = [&]( const ShaderType type )
        {
            for ( const auto& [varType, name] : shaderVaryings )
            {
                AppendToShaderHeader( type, Util::StringFormat( "    {} {};", varType, name ) );
            }
            AppendToShaderHeader( type, "#if defined(HAS_VELOCITY)" );
            AppendToShaderHeader( type, "    vec4 _prevVertexWVP;" );
            AppendToShaderHeader( type, "#endif //HAS_VELOCITY" );
            
            AppendToShaderHeader( type, "#if defined(ENABLE_TBN)" );
            AppendToShaderHeader( type, "    mat3 _tbnWV;" );
            AppendToShaderHeader( type, "#endif //ENABLE_TBN" );
        };

        // Initialize GLSW
        I32 glswState = glswGetCurrentContext() ? 1 : -1;

        if (glswState == -1) 
        {
            glswState = glswInit();
            DIVIDE_GPU_ASSERT( glswState == 1 );
        }

        const U16 reflectionProbeRes = to_U16( nextPOW2( CLAMPED( to_U32( config.rendering.reflectionProbeResolution ), 16u, 4096u ) - 1u ) );

        static_assert(Config::MAX_BONE_COUNT_PER_NODE <= 1024, "ShaderProgram error: too many bones per vert. Can't fit inside UBO");

        // Add our engine specific defines and various code pieces to every GLSL shader
        // Add version as the first shader statement, followed by copyright notice
        AppendToShaderHeader( ShaderType::COUNT, renderingAPI == RenderAPI::OpenGL ? "#version 460 core" : "#version 450" );
        AppendToShaderHeader( ShaderType::COUNT, "//_PROGRAM_NAME_\\" );
        AppendToShaderHeader( ShaderType::COUNT, "/*Copyright (c) 2018 DIVIDE-Studio*/" );
        AppendToShaderHeader( ShaderType::COUNT, "/*Copyright (c) 2009 Ionut Cava*/" );

        if ( renderingAPI == RenderAPI::OpenGL )
        {
            //AppendToShaderHeader(ShaderType::COUNT, "#extension GL_ARB_gpu_shader5 : require");
            AppendToShaderHeader( ShaderType::COUNT, "#define TARGET_OPENGL" );
            AppendToShaderHeader( ShaderType::COUNT, "#define dvd_VertexIndex gl_VertexID" );
            AppendToShaderHeader( ShaderType::COUNT, "#define dvd_InstanceIndex gl_InstanceID" );
            AppendToShaderHeader( ShaderType::COUNT, "#define DVD_GL_BASE_INSTANCE gl_BaseInstance" );
            AppendToShaderHeader( ShaderType::COUNT, "#define DVD_GL_BASE_VERTEX gl_BaseVertex" );
            AppendToShaderHeader( ShaderType::COUNT, "#define DVD_GL_DRAW_ID gl_DrawID" );
        }
        else
        {
            AppendToShaderHeader( ShaderType::COUNT, "#extension GL_ARB_shader_draw_parameters : require" );
            AppendToShaderHeader( ShaderType::COUNT, "#define TARGET_VULKAN" );
            AppendToShaderHeader( ShaderType::COUNT, "#define dvd_VertexIndex gl_VertexIndex" );
            AppendToShaderHeader( ShaderType::COUNT, "#define dvd_InstanceIndex gl_InstanceIndex" );
            AppendToShaderHeader( ShaderType::COUNT, "#define DVD_GL_BASE_INSTANCE gl_BaseInstanceARB" );
            AppendToShaderHeader( ShaderType::COUNT, "#define DVD_GL_BASE_VERTEX gl_BaseVertexARB" );
            AppendToShaderHeader( ShaderType::COUNT, "#define DVD_GL_DRAW_ID gl_DrawIDARB" );
        }

        AppendToShaderHeader( ShaderType::MESH_NV, "#extension GL_NV_mesh_shader : require");
        AppendToShaderHeader( ShaderType::TASK_NV, "#extension GL_NV_mesh_shader : require");

        AppendToShaderHeader( ShaderType::COUNT, crossTypeGLSLHLSL );

        // Add current build environment information to the shaders
        if constexpr ( Config::Build::IS_DEBUG_BUILD )
        {
            AppendToShaderHeader( ShaderType::COUNT, "#define _DEBUG" );
        }
        else if constexpr ( Config::Build::IS_PROFILE_BUILD )
        {
            AppendToShaderHeader( ShaderType::COUNT, "#define _PROFILE" );
        }
        else
        {
            AppendToShaderHeader( ShaderType::COUNT, "#define _RELEASE" );
        }
        AppendToShaderHeader( ShaderType::COUNT, "#define CONCATENATE_IMPL(s1, s2) s1 ## _ ## s2" );
        AppendToShaderHeader( ShaderType::COUNT, "#define CONCATENATE(s1, s2) CONCATENATE_IMPL(s1, s2)" );

        // Shader stage level reflection system. A shader stage must know what stage it's used for
        AppendToShaderHeader( ShaderType::VERTEX, "#define VERT_SHADER" );
        AppendToShaderHeader( ShaderType::FRAGMENT, "#define FRAG_SHADER" );
        AppendToShaderHeader( ShaderType::GEOMETRY, "#define GEOM_SHADER" );
        AppendToShaderHeader( ShaderType::COMPUTE, "#define COMPUTE_SHADER" );
        AppendToShaderHeader( ShaderType::TESSELLATION_EVAL, "#define TESS_EVAL_SHADER" );
        AppendToShaderHeader( ShaderType::TESSELLATION_CTRL, "#define TESS_CTRL_SHADER" );

        // This line gets replaced in every shader at load with the custom list of defines specified by the material
        AppendToShaderHeader( ShaderType::COUNT, "_CUSTOM_DEFINES__" );

        constexpr float Z_TEST_SIGMA = 0.00001f;// 1.f / U8_MAX;
        // ToDo: Automate adding of buffer bindings by using, for example, a TypeUtil::bufferBindingToString -Ionut
        AppendToShaderHeader( ShaderType::COUNT, "#define ALPHA_DISCARD_THRESHOLD " + Util::to_string( Config::ALPHA_DISCARD_THRESHOLD ) + "f" );
        AppendToShaderHeader( ShaderType::COUNT, "#define Z_TEST_SIGMA " + Util::to_string( Z_TEST_SIGMA ) + "f" );
        AppendToShaderHeader( ShaderType::COUNT, "#define INV_Z_TEST_SIGMA " + Util::to_string( 1.f - Z_TEST_SIGMA ) + "f" );
        AppendToShaderHeader( ShaderType::COUNT, "#define MAX_CSM_SPLITS_PER_LIGHT " + Util::to_string( Config::Lighting::MAX_CSM_SPLITS_PER_LIGHT ) );
        AppendToShaderHeader( ShaderType::COUNT, "#define MAX_SHADOW_CASTING_LIGHTS " + Util::to_string( Config::Lighting::MAX_SHADOW_CASTING_LIGHTS ) );
        AppendToShaderHeader( ShaderType::COUNT, "#define MAX_SHADOW_CASTING_DIR_LIGHTS " + Util::to_string( Config::Lighting::MAX_SHADOW_CASTING_DIRECTIONAL_LIGHTS ) );
        AppendToShaderHeader( ShaderType::COUNT, "#define MAX_SHADOW_CASTING_POINT_LIGHTS " + Util::to_string( Config::Lighting::MAX_SHADOW_CASTING_POINT_LIGHTS ) );
        AppendToShaderHeader( ShaderType::COUNT, "#define MAX_SHADOW_CASTING_SPOT_LIGHTS " + Util::to_string( Config::Lighting::MAX_SHADOW_CASTING_SPOT_LIGHTS ) );
        AppendToShaderHeader( ShaderType::COUNT, "#define WORLD_AO_LAYER_INDEX " + Util::to_string( ShadowMap::WORLD_AO_LAYER_INDEX ) );
        AppendToShaderHeader( ShaderType::COUNT, "#define MAX_LIGHTS " + Util::to_string( Config::Lighting::MAX_ACTIVE_LIGHTS_PER_FRAME ) );
        AppendToShaderHeader( ShaderType::COUNT, "#define MAX_VISIBLE_NODES " + Util::to_string( Config::MAX_VISIBLE_NODES ) );
        AppendToShaderHeader( ShaderType::COUNT, "#define MAX_CONCURRENT_MATERIALS " + Util::to_string( Config::MAX_CONCURRENT_MATERIALS ) );
        AppendToShaderHeader( ShaderType::COUNT, "#define MAX_CLIP_PLANES " + Util::to_string( Config::MAX_CLIP_DISTANCES ) );
        AppendToShaderHeader( ShaderType::COUNT, "#define MAX_CULL_DISTANCES " + Util::to_string( Config::MAX_CULL_DISTANCES ) );
        AppendToShaderHeader( ShaderType::COUNT, "#define TARGET_ACCUMULATION " + Util::to_string( to_base( GFXDevice::ScreenTargets::ACCUMULATION ) ) );
        AppendToShaderHeader( ShaderType::COUNT, "#define TARGET_ALBEDO " + Util::to_string( to_base( GFXDevice::ScreenTargets::ALBEDO ) ) );
        AppendToShaderHeader( ShaderType::COUNT, "#define TARGET_VELOCITY " + Util::to_string( to_base( GFXDevice::ScreenTargets::VELOCITY ) ) );
        AppendToShaderHeader( ShaderType::COUNT, "#define TARGET_NORMALS " + Util::to_string( to_base( GFXDevice::ScreenTargets::NORMALS ) ) );
        AppendToShaderHeader( ShaderType::COUNT, "#define TARGET_REVEALAGE " + Util::to_string( to_base( GFXDevice::ScreenTargets::REVEALAGE ) ) );
        AppendToShaderHeader( ShaderType::COUNT, "#define TARGET_MODULATE " + Util::to_string( to_base( GFXDevice::ScreenTargets::MODULATE ) ) );
        AppendToShaderHeader( ShaderType::COUNT, "#define CLUSTERS_X_THREADS " + Util::to_string( Config::Lighting::ClusteredForward::CLUSTERS_X_THREADS ) );
        AppendToShaderHeader( ShaderType::COUNT, "#define CLUSTERS_Y_THREADS " + Util::to_string( Config::Lighting::ClusteredForward::CLUSTERS_Y_THREADS ) );
        AppendToShaderHeader( ShaderType::COUNT, "#define CLUSTERS_Z_THREADS " + Util::to_string( Config::Lighting::ClusteredForward::CLUSTERS_Z_THREADS ) );
        AppendToShaderHeader( ShaderType::COUNT, "#define CLUSTERS_X " + Util::to_string( Config::Lighting::ClusteredForward::CLUSTERS_X ) );
        AppendToShaderHeader( ShaderType::COUNT, "#define CLUSTERS_Y " + Util::to_string( Config::Lighting::ClusteredForward::CLUSTERS_Y ) );
        AppendToShaderHeader( ShaderType::COUNT, "#define CLUSTERS_Z " + Util::to_string( Config::Lighting::ClusteredForward::CLUSTERS_Z ) );
        AppendToShaderHeader( ShaderType::COUNT, "#define SKY_LIGHT_LAYER_IDX " + Util::to_string( SceneEnvironmentProbePool::SkyProbeLayerIndex() ) );
        AppendToShaderHeader( ShaderType::COUNT, "#define MAX_LIGHTS_PER_CLUSTER " + Util::to_string( config.rendering.numLightsPerCluster ) );
        AppendToShaderHeader( ShaderType::COUNT, "#define REFLECTION_PROBE_RESOLUTION " + Util::to_string( reflectionProbeRes ) );
        AppendToShaderHeader( ShaderType::COUNT, "#define REFLECTION_PROBE_MIP_COUNT " + Util::to_string( to_U32( std::log2( reflectionProbeRes ) ) ) );

        AppendResourceBindingSlots( renderingAPI == RenderAPI::OpenGL );

        for ( U8 i = 0u; i < to_base( TextureOperation::COUNT ); ++i )
        {
            AppendToShaderHeader( ShaderType::COUNT, Util::StringFormat( "#define TEX_{} {}", TypeUtil::TextureOperationToString( static_cast<TextureOperation>(i) ), i ).c_str() );
        }
        AppendToShaderHeader( ShaderType::COUNT, Util::StringFormat( "#define WORLD_X_AXIS vec3({:1.1f},{:1.1f},{:1.1f})", WORLD_X_AXIS.x, WORLD_X_AXIS.y, WORLD_X_AXIS.z ) );
        AppendToShaderHeader( ShaderType::COUNT, Util::StringFormat( "#define WORLD_Y_AXIS vec3({:1.1f},{:1.1f},{:1.1f})", WORLD_Y_AXIS.x, WORLD_Y_AXIS.y, WORLD_Y_AXIS.z ) );
        AppendToShaderHeader( ShaderType::COUNT, Util::StringFormat( "#define WORLD_Z_AXIS vec3({:1.1f},{:1.1f},{:1.1f})", WORLD_Z_AXIS.x, WORLD_Z_AXIS.y, WORLD_Z_AXIS.z ) );
        AppendToShaderHeader( ShaderType::COUNT, "#define M_LUMA_VEC vec3(0.2126f, 0.7152f, 0.0722f)");
        AppendToShaderHeader( ShaderType::COUNT, "#define GET_LUMA_VEC3(X) dot(X, M_LUMA_VEC)");
        AppendToShaderHeader( ShaderType::COUNT, "#define M_EPSILON 1e-5f" );
        AppendToShaderHeader( ShaderType::COUNT, "#define M_PI 3.14159265358979323846" );
        AppendToShaderHeader( ShaderType::COUNT, "#define M_PI_DIV_2 1.57079632679489661923" );
        AppendToShaderHeader( ShaderType::COUNT, "#define INV_M_PI 0.31830988618379067153" );
        AppendToShaderHeader( ShaderType::COUNT, "#define TWO_M_PI 6.28318530717958647692" );
        AppendToShaderHeader( ShaderType::COUNT, "#define EULER_CONST 2.71828182845904523536" );

        AppendToShaderHeader( ShaderType::COUNT, "#define ACCESS_RW" );
        AppendToShaderHeader( ShaderType::COUNT, "#define ACCESS_R readonly" );
        AppendToShaderHeader( ShaderType::COUNT, "#define ACCESS_W writeonly" );

        AppendToShaderHeader( ShaderType::VERTEX, "#define COMP_ONLY_W readonly" );
        AppendToShaderHeader( ShaderType::VERTEX, "#define COMP_ONLY_R" );
        AppendToShaderHeader( ShaderType::VERTEX, "#define COMP_ONLY_RW readonly" );
        AppendToShaderHeader( ShaderType::TESSELLATION_CTRL, "#define COMP_ONLY_W readonly" );
        AppendToShaderHeader( ShaderType::TESSELLATION_CTRL, "#define COMP_ONLY_R" );
        AppendToShaderHeader( ShaderType::TESSELLATION_CTRL, "#define COMP_ONLY_RW readonly" );
        AppendToShaderHeader( ShaderType::TESSELLATION_EVAL, "#define COMP_ONLY_W readonly" );
        AppendToShaderHeader( ShaderType::TESSELLATION_EVAL, "#define COMP_ONLY_R" );
        AppendToShaderHeader( ShaderType::TESSELLATION_EVAL, "#define COMP_ONLY_RW readonly" );
        AppendToShaderHeader( ShaderType::GEOMETRY, "#define COMP_ONLY_W readonly" );
        AppendToShaderHeader( ShaderType::GEOMETRY, "#define COMP_ONLY_R" );
        AppendToShaderHeader( ShaderType::GEOMETRY, "#define COMP_ONLY_RW readonly" );
        AppendToShaderHeader( ShaderType::FRAGMENT, "#define COMP_ONLY_W readonly" );
        AppendToShaderHeader( ShaderType::FRAGMENT, "#define COMP_ONLY_R" );
        AppendToShaderHeader( ShaderType::FRAGMENT, "#define COMP_ONLY_RW readonly" );

        AppendToShaderHeader( ShaderType::COMPUTE, "#define COMP_ONLY_W ACCESS_W" );
        AppendToShaderHeader( ShaderType::COMPUTE, "#define COMP_ONLY_R ACCESS_R" );
        AppendToShaderHeader( ShaderType::COMPUTE, "#define COMP_ONLY_RW ACCESS_RW" );

        AppendToShaderHeader( ShaderType::COUNT, "#define AND(a, b) (a * b)" );
        AppendToShaderHeader( ShaderType::COUNT, "#define OR(a, b) min(a + b, 1.f)" );

        AppendToShaderHeader( ShaderType::COUNT, "#define XOR(a, b) ((a + b) % 2)" );
        AppendToShaderHeader( ShaderType::COUNT, "#define NOT(X) (1.f - X)" );
        AppendToShaderHeader( ShaderType::COUNT, "#define Squared(X) (X * X)" );
        AppendToShaderHeader( ShaderType::COUNT, "#define Round(X) floor((X) + .5f)" );
        AppendToShaderHeader( ShaderType::COUNT, "#define Saturate(X) clamp(X, 0, 1)" );
        AppendToShaderHeader( ShaderType::COUNT, "#define Mad(a, b, c) (a * b + c)" );

        AppendToShaderHeader( ShaderType::COUNT, "#define GLOBAL_WATER_BODIES_COUNT " + Util::to_string( GLOBAL_WATER_BODIES_COUNT ) );
        AppendToShaderHeader( ShaderType::COUNT, "#define GLOBAL_PROBE_COUNT " + Util::to_string( GLOBAL_PROBE_COUNT ) );
        AppendToShaderHeader( ShaderType::COUNT, "#define MATERIAL_TEXTURE_COUNT 16" );

        AppendToShaderHeader( ShaderType::VERTEX, "#define MAX_BONE_COUNT_PER_NODE " + Util::to_string( Config::MAX_BONE_COUNT_PER_NODE ) );
        AppendToShaderHeader( ShaderType::VERTEX, "#define ATTRIB_POSITION " + Util::to_string( to_base( AttribLocation::POSITION ) ) );
        AppendToShaderHeader( ShaderType::VERTEX, "#define ATTRIB_TEXCOORD " + Util::to_string( to_base( AttribLocation::TEXCOORD ) ) );
        AppendToShaderHeader( ShaderType::VERTEX, "#define ATTRIB_NORMAL " + Util::to_string( to_base( AttribLocation::NORMAL ) ) );
        AppendToShaderHeader( ShaderType::VERTEX, "#define ATTRIB_TANGENT " + Util::to_string( to_base( AttribLocation::TANGENT ) ) );
        AppendToShaderHeader( ShaderType::VERTEX, "#define ATTRIB_COLOR " + Util::to_string( to_base( AttribLocation::COLOR ) ) );
        AppendToShaderHeader( ShaderType::VERTEX, "#define ATTRIB_BONE_WEIGHT " + Util::to_string( to_base( AttribLocation::BONE_WEIGHT ) ) );
        AppendToShaderHeader( ShaderType::VERTEX, "#define ATTRIB_BONE_INDICE " + Util::to_string( to_base( AttribLocation::BONE_INDICE ) ) );
        AppendToShaderHeader( ShaderType::VERTEX, "#define ATTRIB_WIDTH " + Util::to_string( to_base( AttribLocation::WIDTH ) ) );
        AppendToShaderHeader( ShaderType::VERTEX, "#define ATTRIB_GENERIC " + Util::to_string( to_base( AttribLocation::GENERIC ) ) );
        AppendToShaderHeader( ShaderType::COUNT,  "#define ATTRIB_FREE_START 12" );
        AppendToShaderHeader( ShaderType::FRAGMENT, "#define MAX_SHININESS " + Util::to_string( Material::MAX_SHININESS ) );

        const string interfaceLocationString = "layout(location = 0) ";

        for ( U8 i = 0u; i < to_U8( ShadingMode::COUNT ) + 1u; ++i )
        {
            const ShadingMode mode = static_cast<ShadingMode>(i);
            AppendToShaderHeader( ShaderType::FRAGMENT, Util::StringFormat( "#define SHADING_{} {}", TypeUtil::ShadingModeToString( mode ), i ) );
        }

        AppendToShaderHeader( ShaderType::FRAGMENT, Util::StringFormat( "#define SHADING_COUNT {}", to_base( ShadingMode::COUNT ) ) );

        for ( U8 i = 0u; i < to_U8( MaterialDebugFlag::COUNT ) + 1u; ++i )
        {
            const MaterialDebugFlag flag = static_cast<MaterialDebugFlag>(i);
            AppendToShaderHeader( ShaderType::FRAGMENT, Util::StringFormat( "#define DEBUG_{} {}", TypeUtil::MaterialDebugFlagToString( flag ), i ) );
        }

        AppendToShaderHeader( ShaderType::COUNT, "#if defined(PRE_PASS) || defined(SHADOW_PASS)" );
        AppendToShaderHeader( ShaderType::COUNT, "#   define DEPTH_PASS" );
        AppendToShaderHeader( ShaderType::COUNT, "#endif //PRE_PASS || SHADOW_PASS" );

        AppendToShaderHeader( ShaderType::COUNT, "#if defined(COMPUTE_TBN) && !defined(ENABLE_TBN)" );
        AppendToShaderHeader( ShaderType::COUNT, "#   define ENABLE_TBN" );
        AppendToShaderHeader( ShaderType::COUNT, "#endif //COMPUTE_TBN && !ENABLE_TBN" );

        AppendToShaderHeader( ShaderType::GEOMETRY, "#if !defined(INPUT_PRIMITIVE_SIZE)" );
        AppendToShaderHeader( ShaderType::GEOMETRY, "#   define INPUT_PRIMITIVE_SIZE 1" );
        AppendToShaderHeader( ShaderType::GEOMETRY, "#endif //!INPUT_PRIMITIVE_SIZE" );

        AppendToShaderHeader( ShaderType::TESSELLATION_CTRL, "#if !defined(TESSELLATION_OUTPUT_VERTICES)" );
        AppendToShaderHeader( ShaderType::TESSELLATION_CTRL, "#   define TESSELLATION_OUTPUT_VERTICES 4" );
        AppendToShaderHeader( ShaderType::TESSELLATION_CTRL, "#endif //!TESSELLATION_OUTPUT_VERTICES" );

        // Vertex shader output
        AppendToShaderHeader( ShaderType::VERTEX, interfaceLocationString + "out Data {" );
        addVaryings( ShaderType::VERTEX );
        AppendToShaderHeader( ShaderType::VERTEX, "} _out;\n" );

        // Tessellation Control shader input
        AppendToShaderHeader( ShaderType::TESSELLATION_CTRL, interfaceLocationString + "in Data {" );
        addVaryings( ShaderType::TESSELLATION_CTRL );
        AppendToShaderHeader( ShaderType::TESSELLATION_CTRL, "} _in[gl_MaxPatchVertices];\n" );

        // Tessellation Control shader output
        AppendToShaderHeader( ShaderType::TESSELLATION_CTRL, interfaceLocationString + "out Data {" );
        addVaryings( ShaderType::TESSELLATION_CTRL );
        AppendToShaderHeader( ShaderType::TESSELLATION_CTRL, "} _out[TESSELLATION_OUTPUT_VERTICES];\n" );

        AppendToShaderHeader( ShaderType::TESSELLATION_CTRL, getPassData( ShaderType::TESSELLATION_CTRL ) );

        // Tessellation Eval shader input
        AppendToShaderHeader( ShaderType::TESSELLATION_EVAL, interfaceLocationString + "in Data {" );
        addVaryings( ShaderType::TESSELLATION_EVAL );
        AppendToShaderHeader( ShaderType::TESSELLATION_EVAL, "} _in[gl_MaxPatchVertices];\n" );

        // Tessellation Eval shader output
        AppendToShaderHeader( ShaderType::TESSELLATION_EVAL, interfaceLocationString + "out Data {" );
        addVaryings( ShaderType::TESSELLATION_EVAL );
        AppendToShaderHeader( ShaderType::TESSELLATION_EVAL, "} _out;\n" );

        AppendToShaderHeader( ShaderType::TESSELLATION_EVAL, getPassData( ShaderType::TESSELLATION_EVAL ) );

        // Geometry shader input
        AppendToShaderHeader( ShaderType::GEOMETRY, interfaceLocationString + "in Data {" );
        addVaryings( ShaderType::GEOMETRY );
        AppendToShaderHeader( ShaderType::GEOMETRY, "} _in[INPUT_PRIMITIVE_SIZE];\n" );

        // Geometry shader output
        AppendToShaderHeader( ShaderType::GEOMETRY, interfaceLocationString + "out Data {" );
        addVaryings( ShaderType::GEOMETRY );
        AppendToShaderHeader( ShaderType::GEOMETRY, "} _out;\n" );

        AppendToShaderHeader( ShaderType::GEOMETRY, getPassData( ShaderType::GEOMETRY ) );

        // Fragment shader input
        AppendToShaderHeader( ShaderType::FRAGMENT, interfaceLocationString + "in Data {" );
        addVaryings( ShaderType::FRAGMENT );
        AppendToShaderHeader( ShaderType::FRAGMENT, "} _in;\n" );

        AppendToShaderHeader( ShaderType::VERTEX, "#define VAR _out" );
        AppendToShaderHeader( ShaderType::TESSELLATION_CTRL, "#define VAR _in[gl_InvocationID]" );
        AppendToShaderHeader( ShaderType::TESSELLATION_EVAL, "#define VAR _in[0]" );
        AppendToShaderHeader( ShaderType::GEOMETRY, "#define VAR _in" );
        AppendToShaderHeader( ShaderType::FRAGMENT, "#define VAR _in" );

        AppendToShaderHeader( ShaderType::COUNT, "//_CUSTOM_UNIFORMS_\\" );
        AppendToShaderHeader( ShaderType::COUNT, "//_PUSH_CONSTANTS_DEFINE_\\" );

        // Check initialization status for GLSL and glsl-optimizer
        return glswState == 1;
    }

    ShaderModuleDescriptor::ShaderModuleDescriptor( ShaderType type, const Str<64>& file, const Str<64>& variant )
        : _sourceFile( file ), _variant( variant ), _moduleType( type )
    {
    }

    std::atomic_bool ShaderModule::s_modulesRemoved;
    SharedMutex ShaderModule::s_shaderNameLock;
    NO_DESTROY ShaderModule::ShaderMap ShaderModule::s_shaderNameMap;

    void ShaderModule::Idle( const bool fast )
    {
        if ( fast )
        {
            NOP();
            return;
        }

        bool expected = true;
        if ( s_modulesRemoved.compare_exchange_strong(expected, false) )
        {
            LockGuard<SharedMutex> w_lock( s_shaderNameLock );
            for ( auto it = s_shaderNameMap.begin(); it != s_shaderNameMap.end(); )
            {
                ShaderModule* shaderModule = it->second.get();
                if ( !shaderModule->_inUse && shaderModule->_lastUsedFrame + MAX_FRAME_LIFETIME < GFXDevice::FrameCount() )
                {
                    Console::warnfn(LOCALE_STR("SHADER_MODULE_EXPIRED"), shaderModule->_name.c_str());
                    it = s_shaderNameMap.erase(it);
                }
                else
                {
                    ++it;
                }
            }
        }
    }

    void ShaderModule::InitStaticData()
    {
        NOP();
    }

    void ShaderModule::DestroyStaticData()
    {
        LockGuard<SharedMutex> w_lock( s_shaderNameLock );
        s_shaderNameMap.clear();
    }

    ShaderModule* ShaderModule::GetShader( const std::string_view name )
    {
        SharedLock<SharedMutex> r_lock( s_shaderNameLock );
        return GetShaderLocked( name );
    }

    ShaderModule* ShaderModule::GetShaderLocked( const std::string_view name )
    {
        // Try to find the shader
        const ShaderMap::iterator it = s_shaderNameMap.find( _ID( name ) );
        if ( it != std::end( s_shaderNameMap ) )
        {
            return it->second.get();
        }

        return nullptr;
    }

    ShaderModule::ShaderModule( GFXDevice& context, const std::string_view name, const U32 generation )
        : GUIDWrapper()
        , GraphicsResource( context, Type::SHADER, getGUID(), _ID( name ) )
        , _name( name )
        , _generation( generation )
    {
    }

    ShaderModule::~ShaderModule()
    {
    }

    void ShaderModule::registerParent( ShaderProgram* parent )
    {
        DIVIDE_ASSERT(parent != nullptr);

        LockGuard<Mutex> w_lock(_parentLock);
        for ( ShaderProgram* it : _parents )
        {
            if ( it->getGUID() == parent->getGUID() )
            {
                return;
            }
        }

        _parents.push_back(parent);
        _inUse = true;
    }

    void ShaderModule::deregisterParent( ShaderProgram* parent )
    {
        DIVIDE_ASSERT( parent != nullptr );

        const I64 targetGUID = parent->getGUID();
        LockGuard<Mutex> w_lock( _parentLock );
        if ( dvd_erase_if( _parents,
                           [targetGUID]( ShaderProgram* it )
                           {
                               return it->getGUID() == targetGUID;
                           } ) )
        {
            if ( _parents.empty() )
            {
                _inUse = false;
                _lastUsedFrame = GFXDevice::FrameCount();
                s_modulesRemoved.store(true);
            }
        }
    }

    ShaderProgram::ShaderProgram( PlatformContext& context, const ResourceDescriptor<ShaderProgram>& descriptor )
        : CachedResource( descriptor, "ShaderProgram" )
        , GraphicsResource( context.gfx(), Type::SHADER_PROGRAM, getGUID(), _ID( resourceName() ) )
        , _highPriority( descriptor.flag() )
        , _descriptor( descriptor._propertyDescriptor )
    {
        if ( assetName().empty() )
        {
            assetName( resourceName() );
        }

        if ( assetLocation().empty() )
        {
            assetLocation( Paths::g_shadersLocation );
        }

        DIVIDE_ASSERT ( !assetName().empty() );
        _useShaderCache = _descriptor._useShaderCache;
        s_shaderCount.fetch_add( 1, std::memory_order_relaxed );
    }

    ShaderProgram::~ShaderProgram()
    {
        Console::d_printfn( LOCALE_STR( "SHADER_PROGRAM_REMOVE" ), resourceName().c_str() );
        s_shaderCount.fetch_sub( 1, std::memory_order_relaxed );
    }

    bool ShaderProgram::load( PlatformContext& context )
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Streaming );

        hashMap<U64, PerFileShaderData> loadDataByFile{};
        if ( loadInternal( loadDataByFile, false ))
        {
            return CachedResource::load( context );
        }

        return false;
    }

    bool ShaderProgram::postLoad()
    {
        RegisterShaderProgram( this );
        return CachedResource::postLoad();
    }

    bool ShaderProgram::unload()
    {
        // Our GPU Arena will clean up the memory, but we should still destroy these
        _uniformBlockBuffers.clear();
        // Unregister the program from the manager
        DIVIDE_EXPECTED_CALL( UnregisterShaderProgram( this ) );

        return true;
    }

    /// Rebuild the specified shader stages from source code
    bool ShaderProgram::recompile( bool& skipped )
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Graphics );

        skipped = true;
        if ( getState() == ResourceState::RES_LOADED )
        {
            if ( validatePreBind( false ) != ShaderResult::OK )
            {
                return false;
            }

            skipped = false;
            hashMap<U64, PerFileShaderData> loadDataByFile{};
            return loadInternal( loadDataByFile, true );
        }

        return false;
    }

    ShaderResult ShaderProgram::validatePreBind( [[maybe_unused]] const bool rebind)
    {
        return ShaderResult::OK;
    }

    void ShaderProgram::OnThreadCreated( const GFXDevice& gfx, [[maybe_unused]] const size_t threadIndex, [[maybe_unused]] const std::thread::id& threadID, [[maybe_unused]] const bool isMainRenderThread )
    {
        Preprocessor::OnThreadCreated();

        DIVIDE_EXPECTED_CALL( InitGLSW( gfx.renderAPI(), gfx.context().config() ) );
    }

    void ShaderProgram::Idle( [[maybe_unused]] PlatformContext& platformContext, const bool fast )
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Graphics );

        ShaderModule::Idle( fast );

        if ( !s_recompileFailedQueue.empty() )
        {
            ShaderQueueEntry& entry = s_recompileFailedQueue.top();
            if ( entry._queueDelay > 0u )
            {
                --entry._queueDelay;
            }
            else
            {
                s_recompileQueue.push( entry );
                s_recompileFailedQueue.pop();
            }
        }

        // If we don't have any shaders queued for recompilation, return early
        if ( !s_recompileQueue.empty() )
        {
            // Else, recompile the top program from the queue
            ShaderQueueEntry entry = s_recompileQueue.top();
            if ( !entry._program->recompile() )
            {
                Console::errorfn( LOCALE_STR( "ERROR_SHADER_RECOMPILE_FAILED" ), entry._program->resourceName().c_str() );

                // We can delay a recomputation up to an interval of a minute
                if ( entry._queueDelayHighWaterMark < Config::TARGET_FRAME_RATE * 60)
                {
                    entry._queueDelayHighWaterMark += 1u;
                    entry._queueDelay = entry._queueDelayHighWaterMark;
                }
                s_recompileFailedQueue.push(entry);
            }

            s_recompileQueue.pop();
        }
    }

    void ShaderProgram::InitStaticData()
    {
        ShaderModule::InitStaticData();
    }

    void ShaderProgram::DestroyStaticData()
    {
        ShaderModule::DestroyStaticData();
    }

    /// Calling this will force a recompilation of all shader stages for the program that matches the name specified
    bool ShaderProgram::RecompileShaderProgram( const std::string_view name )
    {
        bool state = false;

        SharedLock<SharedMutex> lock( s_programLock );

        // Find the shader program
        for ( ShaderProgram* program : s_shaderPrograms )
        {
            DIVIDE_ASSERT( program != nullptr );

            const string shaderName{ program->resourceName().c_str() };
            // Check if the name matches any of the program's name components
            if ( shaderName.find( name ) != Str<256>::npos || shaderName.compare( name ) == 0 )
            {
                // We process every partial match. So add it to the recompilation queue
                s_recompileQueue.push( ShaderQueueEntry{ ._program = program } );
                // Mark as found
                state = true;
            }
        }
        // If no shaders were found, show an error
        if ( !state )
        {
            Console::errorfn( LOCALE_STR( "ERROR_RECOMPILE_NOT_FOUND" ), name );
        }

        return state;
    }

    ErrorCode ShaderProgram::OnStartup( PlatformContext& context )
    {
        RefreshBindingSlots();

        if constexpr ( !Config::Build::IS_SHIPPING_BUILD )
        {
            FileWatcher& watcher = FileWatcherManager::allocateWatcher();
            s_shaderFileWatcherID = watcher.getGUID();
            g_sFileWatcherListener.addIgnoredEndCharacter( '~' );
            g_sFileWatcherListener.addIgnoredExtension( "tmp" );

            const vector<ResourcePath> atomLocations = GetAllAtomLocations();
            for ( const ResourcePath& loc : atomLocations )
            {
                DIVIDE_EXPECTED_CALL( createDirectory( loc ) == FileError::NONE );
                watcher().addWatch( loc.string().c_str(), &g_sFileWatcherListener );
            }
        }

        shaderAtomLocationPrefix[to_base( ShaderType::FRAGMENT )]          = Paths::Shaders::GLSL::g_fragAtomLoc;
        shaderAtomLocationPrefix[to_base( ShaderType::VERTEX )]            = Paths::Shaders::GLSL::g_vertAtomLoc;
        shaderAtomLocationPrefix[to_base( ShaderType::GEOMETRY )]          = Paths::Shaders::GLSL::g_geomAtomLoc;
        shaderAtomLocationPrefix[to_base( ShaderType::TESSELLATION_CTRL )] = Paths::Shaders::GLSL::g_tescAtomLoc;
        shaderAtomLocationPrefix[to_base( ShaderType::TESSELLATION_EVAL )] = Paths::Shaders::GLSL::g_teseAtomLoc;
        shaderAtomLocationPrefix[to_base( ShaderType::COMPUTE )]           = Paths::Shaders::GLSL::g_compAtomLoc;
        shaderAtomLocationPrefix[to_base( ShaderType::MESH_NV )]           = Paths::Shaders::GLSL::g_meshAtomLoc;
        shaderAtomLocationPrefix[to_base( ShaderType::TASK_NV )]           = Paths::Shaders::GLSL::g_taskAtomLoc;
        shaderAtomLocationPrefix[to_base( ShaderType::COUNT )]             = Paths::Shaders::GLSL::g_comnAtomLoc;

        shaderAtomExtensionName[to_base( ShaderType::FRAGMENT )]          = Paths::Shaders::GLSL::g_fragAtomExt;
        shaderAtomExtensionName[to_base( ShaderType::VERTEX )]            = Paths::Shaders::GLSL::g_vertAtomExt;
        shaderAtomExtensionName[to_base( ShaderType::GEOMETRY )]          = Paths::Shaders::GLSL::g_geomAtomExt;
        shaderAtomExtensionName[to_base( ShaderType::TESSELLATION_CTRL )] = Paths::Shaders::GLSL::g_tescAtomExt;
        shaderAtomExtensionName[to_base( ShaderType::TESSELLATION_EVAL )] = Paths::Shaders::GLSL::g_teseAtomExt;
        shaderAtomExtensionName[to_base( ShaderType::COMPUTE )]           = Paths::Shaders::GLSL::g_compAtomExt;
        shaderAtomExtensionName[to_base( ShaderType::MESH_NV )]           = Paths::Shaders::GLSL::g_meshAtomExt;
        shaderAtomExtensionName[to_base( ShaderType::TASK_NV )]           = Paths::Shaders::GLSL::g_taskAtomExt;
        shaderAtomExtensionName[to_base( ShaderType::COUNT )]       = "." + Paths::Shaders::GLSL::g_comnAtomExt;

        for ( U8 i = 0u; i < to_base( ShaderType::COUNT ) + 1; ++i )
        {
            shaderAtomExtensionHash[i] = _ID( shaderAtomExtensionName[i].c_str() );
        }

        const Configuration& config = context.config();
        s_useShaderCache = config.debug.cache.enabled && config.debug.cache.shaders;
        s_targetOpenGL = context.gfx().renderAPI() == RenderAPI::OpenGL;

        FileList list{};
        if ( s_useShaderCache )
        {
            for ( U8 i = 0u; i < to_base( ShaderType::COUNT ) + 1; ++i )
            {
                const ResourcePath& atomLocation = shaderAtomLocationPrefix[i];
                if ( getAllFilesInDirectory( atomLocation, list, shaderAtomExtensionName[i].c_str() ) )
                {
                    for ( const FileEntry& it : list )
                    {
                        s_newestShaderAtomWriteTime = std::max( s_newestShaderAtomWriteTime, it._lastWriteTime );
                    }
                }
                list.resize( 0 );
            }
        }

        return ErrorCode::NO_ERR;
    }

    ErrorCode ShaderProgram::SubmitSetLayouts( GFXDevice& gfx )
    {
        Preprocessor::OnThreadCreated();
        if ( !InitGLSW( gfx.renderAPI(), gfx.context().config() ))
        {
            return ErrorCode::GLSL_INIT_ERROR;
        }

        SpirvHelper::Init();
        return ErrorCode::NO_ERR;
    }

    bool ShaderProgram::OnShutdown()
    {
        SpirvHelper::Finalize();

        while ( !s_recompileQueue.empty() )
        {
            s_recompileQueue.pop();
        }

        s_shaderPrograms.clear();
        s_lastRequestedShaderProgram = {};

        FileWatcherManager::deallocateWatcher( s_shaderFileWatcherID );
        s_shaderFileWatcherID = -1;

        s_shaderCount = 0u;
        s_atoms.clear();
        s_atomIncludes.clear();

        k_commandBufferID = U8_MAX - MAX_BINDINGS_PER_DESCRIPTOR_SET;

        for ( auto& bindings : s_bindingsPerSet )
        {
            bindings.fill( {} );
        }

        return glswGetCurrentContext() == nullptr || glswShutdown() == 1;
    }


    U8 ShaderProgram::GetGLBindingForDescriptorSlot( const DescriptorSetUsage usage, const U8 slot ) noexcept
    {
        return s_bindingsPerSet[to_base( usage )][slot]._glBinding;
    }

    std::pair<DescriptorSetUsage, U8> ShaderProgram::GetDescriptorSlotForGLBinding( const U8 binding, const DescriptorSetBindingType type ) noexcept
    {
        for ( U8 i = 0u; i < to_base( DescriptorSetUsage::COUNT ); ++i )
        {
            const BindingsPerSetArray& bindings = s_bindingsPerSet[i];
            for ( U8 j = 0u; j < bindings.size(); ++j )
            {
                if ( bindings[j]._glBinding == binding && bindings[j]._type == type )
                {
                    return { static_cast<DescriptorSetUsage>(i), j };
                }
            }
        }

        // If we didn't specify any images, we assume per-draw granularity
        return { DescriptorSetUsage::PER_DRAW, binding };
    }

    ShaderProgram::BindingSetData& ShaderProgram::GetBindingSetData() noexcept
    {
        return s_bindingsPerSet;
    }

    void ShaderProgram::RegisterSetLayoutBinding( const DescriptorSetUsage usage, const U8 slot, const DescriptorSetBindingType type, const ShaderStageVisibility visibility )
    {
        DIVIDE_GPU_ASSERT( slot < MAX_BINDINGS_PER_DESCRIPTOR_SET );

        BindingsPerSet& bindingData = s_bindingsPerSet[to_base( usage )][slot];
        bindingData._type = type;
        bindingData._visibility = to_base(visibility);

        switch ( type )
        {
            case DescriptorSetBindingType::COMBINED_IMAGE_SAMPLER:
                bindingData._glBinding = s_textureSlot++;
                break;
            case DescriptorSetBindingType::IMAGE:
                bindingData._glBinding = s_imageSlot++;
                break;
            case DescriptorSetBindingType::SHADER_STORAGE_BUFFER:
            case DescriptorSetBindingType::UNIFORM_BUFFER:
                if ( usage == DescriptorSetUsage::PER_BATCH && slot == 0 )
                {
                    bindingData._glBinding = k_commandBufferID;
                }
                else
                {
                    bindingData._glBinding = visibility == ShaderStageVisibility::COMPUTE ? s_computeBufferSlot++ : s_bufferSlot++;
                    DIVIDE_GPU_ASSERT(bindingData._glBinding < GFXDevice::GetDeviceInformation()._maxSSBOBufferBindings, "Exceeded maximum number of buffer bindings available in the min spec!" );
                }
                break;

            default:
            case DescriptorSetBindingType::COUNT:
                DIVIDE_UNEXPECTED_CALL();
                break;
        }
    }
    
    U32 ShaderProgram::GetBindingCount( const DescriptorSetUsage usage, const DescriptorSetBindingType type )
    {
        DIVIDE_ASSERT( usage != DescriptorSetUsage::COUNT );

        U32 count = 0u;
        if ( usage == DescriptorSetUsage::PER_DRAW )
        {
            switch ( type )
            {
                case DescriptorSetBindingType::COMBINED_IMAGE_SAMPLER: count = to_base( TextureSlot::COUNT ) + 2u; /*{Reflection + Refraction}*/ break;
                case DescriptorSetBindingType::IMAGE: count = 2u; break;
                case DescriptorSetBindingType::UNIFORM_BUFFER:
                case DescriptorSetBindingType::SHADER_STORAGE_BUFFER: count = 4u; break;
                default:
                case DescriptorSetBindingType::COUNT: break;
            }
        }
        else
        {
            for ( const BindingsPerSet& binding : s_bindingsPerSet[to_base( usage )] )
            {
                if ( binding._type == type )
                {
                    ++count;
                }
            }
        }
        
        return count;
    }

    void ShaderProgram::OnBeginFrame([[maybe_unused]] GFXDevice& gfx )
    {
        s_usedShaderPrograms.resize(0);
    }

    void ShaderProgram::OnEndFrame( GFXDevice& gfx )
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Graphics );

        size_t& totalUniformBufferSize = gfx.getPerformanceMetrics()._uniformBufferVRAMUsage;
        totalUniformBufferSize = 0u;

        for ( ShaderProgram* program : s_usedShaderPrograms )
        {
            for ( UniformBlockUploader& block : program->_uniformBlockBuffers )
            {
                block.onFrameEnd();
                totalUniformBufferSize += block.totalBufferSize();
            }
        }
        
    }

    /// Whenever a new program is created, it's registered with the manager
    void ShaderProgram::RegisterShaderProgram( ShaderProgram* shaderProgram )
    {
        DIVIDE_ASSERT( shaderProgram != nullptr );

        LockGuard<SharedMutex> lock( s_programLock );
        for ( ShaderProgram*& program : s_shaderPrograms )
        {
            if ( program == nullptr )
            {
                program = shaderProgram;
                return;
            }
        }
        s_shaderPrograms.push_back(shaderProgram);
    }

    /// Unloading/Deleting a program will unregister it from the manager
    bool ShaderProgram::UnregisterShaderProgram( ShaderProgram* shaderProgram )
    {
        DIVIDE_ASSERT( shaderProgram != nullptr );
        const I64 guid = shaderProgram->getGUID();
        LockGuard<SharedMutex> lock( s_programLock );
        for ( ShaderProgram*& program : s_shaderPrograms )
        {
            if ( program != nullptr && program->getGUID() == guid)
            {
                program = nullptr;
                return true;
            }
        }

        return false;
    }

    void ShaderProgram::RebuildAllShaders()
    {
        SharedLock<SharedMutex> lock( s_programLock );
        for ( ShaderProgram* program : s_shaderPrograms )
        {
            DIVIDE_ASSERT ( program != nullptr );
            s_recompileQueue.push( ShaderQueueEntry{ ._program = program } );
        }
    }

    vector<ResourcePath> ShaderProgram::GetAllAtomLocations()
    {
        NO_DESTROY static vector<ResourcePath> atomLocations;
        if ( atomLocations.empty() )
        {
            // General
            atomLocations.emplace_back( Paths::g_shadersLocation );
            // GLSL
            atomLocations.emplace_back( Paths::Shaders::GLSL::g_GLSLShaderLoc );

            atomLocations.emplace_back( Paths::Shaders::GLSL::g_comnAtomLoc );

            atomLocations.emplace_back( Paths::Shaders::GLSL::g_compAtomLoc );

            atomLocations.emplace_back( Paths::Shaders::GLSL::g_fragAtomLoc );

            atomLocations.emplace_back( Paths::Shaders::GLSL::g_geomAtomLoc );

            atomLocations.emplace_back( Paths::Shaders::GLSL::g_tescAtomLoc );

            atomLocations.emplace_back( Paths::Shaders::GLSL::g_teseAtomLoc );

            atomLocations.emplace_back( Paths::Shaders::GLSL::g_vertAtomLoc );
        }

        return atomLocations;
    }

    const string& ShaderProgram::ShaderFileRead( const ResourcePath& filePath, const std::string_view atomName, const bool recurse, eastl::set<U64>& foundAtomIDsInOut, bool& wasParsed )
    {
        LockGuard<Mutex> w_lock( s_atomLock );
        return ShaderFileReadLocked( filePath, atomName, recurse, foundAtomIDsInOut, wasParsed );
    }

    void ShaderProgram::PreprocessIncludes( const std::string_view name,
                                            string& sourceInOut,
                                            const I32 level,
                                            eastl::set<U64>& foundAtomIDsInOut,
                                            const bool lock )
    {
        if ( level > s_maxHeaderRecursionLevel )
        {
            Console::errorfn( LOCALE_STR( "ERROR_GLSL_INCLUD_LIMIT" ) );
        }

        size_t lineNumber = 1;

        string line;
        string output, includeString;
        output.reserve(sourceInOut.size());

        istringstream input( sourceInOut );

        while ( Util::GetLine( input, line ) )
        {
            const std::string_view directive = !line.empty() ? std::string_view{ line }.substr( 1 ) : "";

            bool isInclude = Util::BeginsWith( line, "#", true ) &&
                            !Util::BeginsWith( directive, "version", true ) &&
                            !Util::BeginsWith( directive, "extension", true ) &&
                            !Util::BeginsWith( directive, "define", true ) &&
                            !Util::BeginsWith( directive, "if", true ) &&
                            !Util::BeginsWith( directive, "else", true ) &&
                            !Util::BeginsWith( directive, "elif", true ) &&
                            !Util::BeginsWith( directive, "endif", true ) &&
                            !Util::BeginsWith( directive, "pragma", true );

            bool skip = false;
            if ( isInclude )
            {
                if ( auto m = ctre::match<Paths::g_includePattern>( line ) )
                {
                    skip = true;

                    const auto includeFile = Util::Trim( m.get<1>().str() );

                    foundAtomIDsInOut.insert( _ID( includeFile.c_str() ) );

                    ShaderType typeIndex = ShaderType::COUNT;
                    bool found = false;
                    // switch will throw warnings due to promotion to int
                    const U64 extHash = _ID( Util::GetTrailingCharacters( includeFile, 4 ).c_str() );
                    for ( U8 i = 0; i < to_base( ShaderType::COUNT ) + 1; ++i )
                    {
                        if ( extHash == shaderAtomExtensionHash[i] )
                        {
                            typeIndex = static_cast<ShaderType>(i);
                            found = true;
                            break;
                        }
                    }

                    DIVIDE_GPU_ASSERT( found, "Invalid shader include type" );
                    bool wasParsed = false;
                    if ( lock )
                    {
                        includeString = ShaderFileRead( shaderAtomLocationPrefix[to_U32( typeIndex )], includeFile, true, foundAtomIDsInOut, wasParsed ).c_str();
                    }
                    else
                    {
                        includeString = ShaderFileReadLocked( shaderAtomLocationPrefix[to_U32( typeIndex )], includeFile, true, foundAtomIDsInOut, wasParsed ).c_str();
                    }
                    if ( includeString.empty() )
                    {
                        Console::errorfn( LOCALE_STR( "ERROR_GLSL_NO_INCLUDE_FILE" ), name, lineNumber, includeFile );
                    }
                    if ( !wasParsed )
                    {
                        PreprocessIncludes( name, includeString, level + 1, foundAtomIDsInOut, lock );
                    }

                    output.append( includeString );
                }
            }

            if (!skip)
            {
                output.append( line.c_str() );
            }

            output.append( "\n" );
            ++lineNumber;
        }

        sourceInOut = output;
    }

    /// Open the file found at 'filePath' matching 'atomName' and return it's source code
    const string& ShaderProgram::ShaderFileReadLocked( const ResourcePath& filePath, const std::string_view atomName, const bool recurse, eastl::set<U64>& foundAtomIDsInOut, bool& wasParsed )
    {
        const U64 atomNameHash = _ID( atomName );
        // See if the atom was previously loaded and still in cache
        const AtomMap::iterator it = s_atoms.find( atomNameHash );

        // If that's the case, return the code from cache
        if ( it != std::cend( s_atoms ) )
        {
            const auto& atoms = s_atomIncludes[atomNameHash];
            for ( const auto& atom : atoms )
            {
                foundAtomIDsInOut.insert( atom );
            }
            wasParsed = true;
            return it->second;
        }

        wasParsed = false;
        // If we forgot to specify an atom location, we have nothing to return
        assert( !filePath.empty() );

        // Open the atom file and add the code to the atom cache for future reference
        string& output = s_atoms[atomNameHash];
        output.clear();
        eastl::set<U64>& atoms = s_atomIncludes[atomNameHash];
        atoms.clear();

        DIVIDE_EXPECTED_CALL( readFile( filePath, atomName, FileType::TEXT, output ) == FileError::NONE );

        if ( recurse )
        {
            PreprocessIncludes( atomName, output, 0, atoms, false );
        }

        for ( const auto& atom : atoms )
        {
            foundAtomIDsInOut.insert( atom );
        }

        // Return the source code
        return output;
    }

    bool ShaderProgram::SaveToCache( const LoadData::ShaderCacheType cache, const LoadData& dataIn, const eastl::set<U64>& atomIDsIn )
    {
        bool ret = false;

        LockGuard<Mutex> rw_lock( g_cacheLock );
        FileError err = FileError::FILE_EMPTY;
        switch ( cache )
        {
            case LoadData::ShaderCacheType::GLSL:
            {
                if ( !dataIn._sourceCodeGLSL.empty() )
                {
                    {
                        err = writeFile( TxtCacheLocation(),
                                         dataIn._shaderName.c_str(),
                                         dataIn._sourceCodeGLSL.c_str(),
                                         dataIn._sourceCodeGLSL.length(),
                                         FileType::TEXT );
                    }

                    if ( err != FileError::NONE )
                    {
                        Console::errorfn( LOCALE_STR( "ERROR_SHADER_SAVE_TEXT_FAILED" ), dataIn._shaderName.c_str() );
                    }
                    else
                    {
                        ret = true;
                    }
                }
            } break;
            case LoadData::ShaderCacheType::SPIRV:
            {
                if ( !dataIn._sourceCodeSpirV.empty() )
                {
                    err = writeFile( SpvCacheLocation(),
                                     SpvTargetName( dataIn._shaderName ).string(),
                                     (bufferPtr)dataIn._sourceCodeSpirV.data(),
                                     dataIn._sourceCodeSpirV.size() * sizeof( SpvWord ),
                                     FileType::BINARY );

                    if ( err != FileError::NONE )
                    {
                        Console::errorfn( LOCALE_STR( "ERROR_SHADER_SAVE_SPIRV_FAILED" ), dataIn._shaderName.c_str() );
                    }
                    else
                    {
                        ret = true;
                    }
                }
            } break;
            case LoadData::ShaderCacheType::REFLECTION:
            {
                ret = Reflection::SaveReflectionData( ReflCacheLocation(), ReflTargetName( dataIn._shaderName ), dataIn._reflectionData, atomIDsIn );
                if ( !ret )
                {
                    Console::errorfn( LOCALE_STR( "ERROR_SHADER_SAVE_REFL_FAILED" ), dataIn._shaderName.c_str() );
                }
            } break;
            default:
            case LoadData::ShaderCacheType::COUNT: break;
        }

        if ( !ret )
        {
            if ( !DeleteCacheLocked( cache, dataIn._shaderName ) )
            {
                NOP();
            }
        }

        return ret;
    }

    bool ShaderProgram::LoadFromCache( const LoadData::ShaderCacheType cache, LoadData& dataInOut, eastl::set<U64>& atomIDsOut )
    {
        if ( !s_useShaderCache )
        {
            return false;
        }

        LockGuard<Mutex> rw_lock( g_cacheLock );
        if ( !ValidateCacheLocked( cache, dataInOut._sourceFile, dataInOut._shaderName ) )
        {
            if ( !DeleteCacheLocked( cache, dataInOut._shaderName ) )
            {
                NOP();
            }

            return false;
        }

        FileError err = FileError::FILE_EMPTY;
        switch ( cache )
        {
            case LoadData::ShaderCacheType::GLSL:
            {
                err = readFile( TxtCacheLocation(),
                                dataInOut._shaderName.c_str(),
                                FileType::TEXT,
                                dataInOut._sourceCodeGLSL );
                return err == FileError::NONE;
            }
            case LoadData::ShaderCacheType::SPIRV:
            {
                std::ifstream tempData;
                {
                    err = readFile( SpvCacheLocation(),
                                    SpvTargetName( dataInOut._shaderName ).string(),
                                    FileType::BINARY,
                                    tempData );
                }

                if ( err == FileError::NONE )
                {
                    tempData.seekg(0, std::ios::end);
                    dataInOut._sourceCodeSpirV.reserve( tempData.tellg() / sizeof( SpvWord ) );
                    tempData.seekg(0);

                    while (!tempData.eof())
                    {
                        SpvWord inWord;
                        tempData.read((char *)&inWord, sizeof(inWord));
                        if (!tempData.eof())
                        {
                            dataInOut._sourceCodeSpirV.push_back(inWord);
                            if (tempData.fail())
                            {
                                return false;
                            }
                        }
                    }
                    
                    return true;
                }

                return false;
            }
            case LoadData::ShaderCacheType::REFLECTION:
            {
                return Reflection::LoadReflectionData( ReflCacheLocation(), ReflTargetName( dataInOut._shaderName ), dataInOut._reflectionData, atomIDsOut );
            }
            default:
            case LoadData::ShaderCacheType::COUNT: break;
        }

        return false;
    }

    bool ShaderProgram::loadInternal( hashMap<U64, PerFileShaderData>& fileData, const bool overwrite )
    {
        // The context is thread_local so each call to this should be thread safe
        if ( overwrite )
        {
            glswClearCurrentContext();
        }
        glswSetPath(( Paths::Shaders::GLSL::g_GLSLShaderLoc.string() + Paths::g_pathSeparator).c_str(), ".glsl" );

        _usedAtomIDs.clear();

        for ( const ShaderModuleDescriptor& shaderDescriptor : _descriptor._modules )
        {
            const U64 fileHash = _ID( shaderDescriptor._sourceFile.data() );
            fileData[fileHash]._modules.push_back( shaderDescriptor );
            ShaderModuleDescriptor& newDescriptor = fileData[fileHash]._modules.back();
            newDescriptor._defines.insert( end( newDescriptor._defines ), begin( _descriptor._globalDefines ), end( _descriptor._globalDefines ) );
            _usedAtomIDs.insert( _ID( shaderDescriptor._sourceFile.c_str() ) );
        }

        U8 blockOffset = 0u;

        Reflection::UniformsSet previousUniforms;

        _uniformBlockBuffers.clear();
        _setUsage.fill( false );

        for ( auto& [fileHash, loadDataPerFile] : fileData )
        {
            for ( const ShaderModuleDescriptor& data : loadDataPerFile._modules )
            {
                const ShaderType type = data._moduleType;
                assert( type != ShaderType::COUNT );

                ShaderProgram::LoadData& stageData = loadDataPerFile._loadData[to_base( data._moduleType )];
                assert( stageData._type == ShaderType::COUNT );

                stageData._type = data._moduleType;
                stageData._sourceFile = data._sourceFile.c_str();
                stageData._sourceName = data._sourceFile.substr( 0, data._sourceFile.find_first_of( "." ) ).c_str();
                stageData._sourceName.append( "." );
                stageData._sourceName.append( Names::shaderTypes[to_U8( type )] );
                if ( !data._variant.empty() )
                {
                    stageData._sourceName.append( ("." + data._variant).c_str() );
                }
                stageData._definesHash = DefinesHash( data._defines );
                stageData._shaderName.append(stageData._sourceName );
                if ( stageData._definesHash != 0u )
                {
                    stageData._shaderName.append( ("." + Util::to_string(stageData._definesHash)).c_str());
                }
                stageData._shaderName.append( ("." + shaderAtomExtensionName[to_U8(type)]).c_str() );

                if ( !loadSourceCode( data._defines, overwrite, stageData, previousUniforms, blockOffset ) )
                {
                    Console::errorfn(LOCALE_STR("ERROR_SHADER_LOAD_SOURCE_CODE_FAILED"), stageData._shaderName.c_str(), overwrite ? "TRUE" : "FALSE");
                    return false;
                }

                if ( !loadDataPerFile._programName.empty() )
                {
                    loadDataPerFile._programName.append( "-" );
                }
                loadDataPerFile._programName.append( stageData._shaderName.c_str() );
            }

            initUniformUploader( loadDataPerFile );
            initDrawDescriptorSetLayout( loadDataPerFile );
        }

        return true;
    }

    void ShaderProgram::initDrawDescriptorSetLayout( const PerFileShaderData& loadData )
    {
        const ShaderLoadData& programLoadData = loadData._loadData;

        const auto SetVisibility = []( BindingsPerSet& binding, const Reflection::DataEntry& entry)
        {
            if ( binding._visibility == to_base( ShaderStageVisibility::COUNT ) )
            {
                binding._visibility = to_base( ShaderStageVisibility::NONE );
            }

            binding._visibility |= entry._stageVisibility;
        };

        for ( const LoadData& stageData : programLoadData )
        {
            const Reflection::Data& data = stageData._reflectionData;
            if ( stageData._type == ShaderType::FRAGMENT )
            {
                fragmentOutputs(data._fragmentOutputs);
            }

            for ( const Reflection::ImageEntry& image : data._images )
            {
                _setUsage[image._bindingSet] = true;
                if ( image._bindingSet != to_base( DescriptorSetUsage::PER_DRAW ) )
                {
                    continue;
                }

                BindingsPerSet& binding = _perDrawDescriptorSetLayout[image._bindingSlot];
                SetVisibility( binding, image );

                if ( image._combinedImageSampler )
                {
                    DIVIDE_GPU_ASSERT( binding._type == DescriptorSetBindingType::COUNT || binding._type == DescriptorSetBindingType::COMBINED_IMAGE_SAMPLER);
                    binding._type = DescriptorSetBindingType::COMBINED_IMAGE_SAMPLER;
                }
                else
                {
                    DIVIDE_GPU_ASSERT( binding._type == DescriptorSetBindingType::COUNT || binding._type == DescriptorSetBindingType::IMAGE );
                    binding._type = DescriptorSetBindingType::IMAGE;
                }
            }

            for ( const Reflection::BufferEntry& buffer : data._buffers )
            {
                _setUsage[buffer._bindingSet] = true;

                if ( buffer._bindingSet != to_base( DescriptorSetUsage::PER_DRAW ) )
                {
                    continue;
                }

                BindingsPerSet& binding = _perDrawDescriptorSetLayout[buffer._bindingSlot];
                SetVisibility( binding, buffer );

                if ( buffer._uniformBuffer )
                {
                    DIVIDE_GPU_ASSERT( binding._type == DescriptorSetBindingType::COUNT || binding._type == DescriptorSetBindingType::UNIFORM_BUFFER );
                    binding._type = DescriptorSetBindingType::UNIFORM_BUFFER;
                }
                else
                {
                    DIVIDE_GPU_ASSERT( binding._type == DescriptorSetBindingType::COUNT || binding._type == DescriptorSetBindingType::SHADER_STORAGE_BUFFER );
                    binding._type = DescriptorSetBindingType::SHADER_STORAGE_BUFFER;
                }
            }
        }
    }

    void ShaderProgram::initUniformUploader( const PerFileShaderData& loadData )
    {
        const ShaderLoadData& programLoadData = loadData._loadData;

        for ( const LoadData& stageData : programLoadData )
        {
            if ( stageData._type == ShaderType::COUNT )
            {
                continue;
            }

            const Reflection::BufferEntry* uniformBlock = Reflection::FindUniformBlock( stageData._reflectionData );

            if ( uniformBlock != nullptr )
            {
                bool found = false;
                for ( UniformBlockUploader& block : _uniformBlockBuffers )
                {
                    const Reflection::BufferEntry& uploaderBlock = block.uniformBlock();
                    if ( uploaderBlock._bindingSet != stageData._reflectionData._uniformBlockBindingSet ||
                         uploaderBlock._bindingSlot != stageData._reflectionData._uniformBlockBindingIndex )
                    {
                        continue;
                    }

                    block.toggleStageVisibility( uniformBlock->_stageVisibility, true );
                    found = true;
                    break;
                }

                if ( !found )
                {
                    _uniformBlockBuffers.emplace_back( _context, loadData._programName.c_str(), *uniformBlock, uniformBlock->_stageVisibility );
                }
            }
        }
    }

    bool ShaderProgram::uploadUniformData( const UniformData& data, DescriptorSet& set, GFX::MemoryBarrierCommand& memCmdInOut )
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Graphics );

        bool ret = false;
        for ( auto& blockBuffer : _uniformBlockBuffers )
        {
            blockBuffer.uploadUniformData( data );

            if ( blockBuffer.commit( set, memCmdInOut ) )
            {
                s_usedShaderPrograms.emplace_back(this );
                ret = true;
            }
        }

        return ret;
    }

    bool ShaderProgram::loadSourceCode( const ModuleDefines& defines, bool reloadExisting, LoadData& loadDataInOut, Reflection::UniformsSet& previousUniformsInOut, U8& blockIndexInOut )
    {
        // Clear existing code
        loadDataInOut._sourceCodeGLSL.resize( 0 );
        loadDataInOut._sourceCodeSpirV.resize( 0 );

        eastl::set<U64> atomIDs;

        bool needGLSL = s_targetOpenGL;
        if ( reloadExisting )
        {
            // Hot reloading will always reparse GLSL source files so the best way to achieve that is to delete cache files
            needGLSL = true;
            // We should have cached the existing shader, so a failure here is NOT expected
            DIVIDE_EXPECTED_CALL( DeleteCache( LoadData::ShaderCacheType::COUNT, loadDataInOut._shaderName ) );
        }

        // Load SPIRV code from cache (if needed)
        if ( reloadExisting || !useShaderCache()  || !LoadFromCache(LoadData::ShaderCacheType::SPIRV, loadDataInOut, atomIDs) )
        {
            needGLSL = true;
        }

        // We either have SPIRV code or we explicitly require GLSL code (e.g. for OpenGL)
        if ( needGLSL )
        {
            // Try and load GLSL code from cache (if needed)
            if ( reloadExisting || !useShaderCache() || !LoadFromCache( LoadData::ShaderCacheType::GLSL, loadDataInOut, atomIDs ) )
            {
                // That failed, so re-parse the code
                loadAndParseGLSL( defines, loadDataInOut, previousUniformsInOut, blockIndexInOut, atomIDs );
                if ( loadDataInOut._sourceCodeGLSL.empty() )
                {
                    // That failed so we have no choice but to bail
                    return false;
                }
                else
                {
                    // That succeeded so save the new cache file for future use
                    SaveToCache( LoadData::ShaderCacheType::GLSL, loadDataInOut, atomIDs );
                }
            }

            // We MUST have GLSL code at this point so now we have too options.
            // We already have SPIRV code and can proceed or we failed loading SPIRV from cache so we must convert GLSL -> SPIRV
            if ( loadDataInOut._sourceCodeSpirV.empty() )
            {
                // We are in situation B: we need SPIRV code, so convert our GLSL code over
                DIVIDE_GPU_ASSERT( !loadDataInOut._sourceCodeGLSL.empty() );
                if ( !SpirvHelper::GLSLtoSPV( loadDataInOut._type, loadDataInOut._sourceCodeGLSL.c_str(), loadDataInOut._sourceCodeSpirV, s_targetOpenGL ) )
                {
                    Console::errorfn( LOCALE_STR( "ERROR_SHADER_CONVERSION_SPIRV_FAILED" ), loadDataInOut._shaderName.c_str() );
                    // We may fail here for WHATEVER reason so bail
                    if ( !DeleteCache( LoadData::ShaderCacheType::GLSL, loadDataInOut._shaderName ) )
                    {
                        NOP();
                    }
                    return false;
                }
                else
                {
                    // We managed to generate good SPIRV so save it to the cache for future use
                    SaveToCache( LoadData::ShaderCacheType::SPIRV, loadDataInOut, atomIDs );
                }
            }
        }

        // Whatever the process to get here was, we need SPIRV to proceed
        DIVIDE_GPU_ASSERT( !loadDataInOut._sourceCodeSpirV.empty() );
        // Time to see if we have any cached reflection data, and, if not, build it
        if ( reloadExisting || !useShaderCache() || !LoadFromCache( LoadData::ShaderCacheType::REFLECTION, loadDataInOut, atomIDs ) )
        {
            // Well, we failed. Time to build our reflection data again
            if ( !SpirvHelper::BuildReflectionData( loadDataInOut._type, loadDataInOut._sourceCodeSpirV, s_targetOpenGL, loadDataInOut._reflectionData ) )
            {
                Console::errorfn( LOCALE_STR( "ERROR_SHADER_REFLECTION_SPIRV_FAILED" ), loadDataInOut._shaderName.c_str() );
                return false;
            }
            // Save reflection data to cache for future use
            SaveToCache( LoadData::ShaderCacheType::REFLECTION, loadDataInOut, atomIDs );
        }
        else if ( loadDataInOut._reflectionData._uniformBlockBindingIndex != Reflection::INVALID_BINDING_INDEX )
        {
            blockIndexInOut = loadDataInOut._reflectionData._uniformBlockBindingIndex - s_uniformsStartOffset;
        }

        if ( !loadDataInOut._sourceCodeGLSL.empty() || !loadDataInOut._sourceCodeSpirV.empty() )
        {
            _usedAtomIDs.insert( begin( atomIDs ), end( atomIDs ) );
            return true;
        }

        return false;
    }

    void ShaderProgram::loadAndParseGLSL( const ModuleDefines& defines,
                                          LoadData& loadDataInOut,
                                          Reflection::UniformsSet& previousUniformsInOut,
                                          U8& blockIndexInOut,
                                          eastl::set<U64>& atomIDsInOut )
    {
        auto& glslCodeOut = loadDataInOut._sourceCodeGLSL;
        glslCodeOut.resize( 0 );

        // Use GLSW to read the appropriate part of the effect file
        // based on the specified stage and properties
        const char* sourceCodeStr = glswGetShader( loadDataInOut._sourceName.c_str() );
        if ( sourceCodeStr != nullptr )
        {
            glslCodeOut.append( sourceCodeStr );
        }

        // GLSW may fail for various reasons (not a valid effect stage, invalid name, etc)
        if ( !glslCodeOut.empty() )
        {

            string header;
            for ( const auto& [defineString, appendPrefix] : defines )
            {
                // Placeholders are ignored
                if ( defineString == "DEFINE_PLACEHOLDER" )
                {
                    continue;
                }

                // We manually add define dressing if needed
                header.append( (appendPrefix ? "#define " : "") + defineString + '\n' );
            }

            for ( const auto& [defineString, appendPrefix] : defines )
            {
                // Placeholders are ignored
                if ( !appendPrefix || defineString == "DEFINE_PLACEHOLDER" )
                {
                    continue;
                }

                // We also add a comment so that we can check what defines we have set because
                // the shader preprocessor strips defines before sending the code to the GPU
                header.append( "/*Engine define: [ " + defineString + " ]*/\n" );
            }
            // And replace in place with our program's headers created earlier
            Util::ReplaceStringInPlace( glslCodeOut, "_CUSTOM_DEFINES__", header );
            
            PreprocessIncludes( resourceName(), glslCodeOut, 0, atomIDsInOut, true );

            if (!Preprocessor::PreProcessMacros( loadDataInOut._shaderName, glslCodeOut ))
            {
                NOP();
            }

            Reflection::PreProcessUniforms( glslCodeOut, loadDataInOut._uniforms );
        }

        if ( !loadDataInOut._uniforms.empty() )
        {
            if ( !previousUniformsInOut.empty() && previousUniformsInOut != loadDataInOut._uniforms )
            {
                ++blockIndexInOut;

                DIVIDE_GPU_ASSERT(blockIndexInOut < 2, "ShaderProgram::load: We only support 2 uniform blocks per shader program at the moment. Batch uniforms from different stages together to reduce usage!");
            }

            loadDataInOut._reflectionData._uniformBlockBindingSet = to_base( DescriptorSetUsage::PER_DRAW );
            loadDataInOut._reflectionData._uniformBlockBindingIndex = blockIndexInOut + s_uniformsStartOffset;

            string& uniformBlock = loadDataInOut._uniformBlock;
            uniformBlock = "layout( ";
            if ( _context.renderAPI() == RenderAPI::Vulkan )
            {
                uniformBlock.append( Util::StringFormat( "set = {}, ", to_base( DescriptorSetUsage::PER_DRAW ) ) );
            }
            uniformBlock.append( "binding = {}, std140 ) uniform {} {{" );

            for ( const Reflection::UniformDeclaration& uniform : loadDataInOut._uniforms )
            {
                uniformBlock.append( Util::StringFormat( "\n    {} {};", uniform._type.c_str(), uniform._name.c_str() ) );
            }
            uniformBlock.append( "\n}} ");
            uniformBlock.append(Util::StringFormat("{};", UNIFORM_BLOCK_NAME));

            for ( const Reflection::UniformDeclaration& uniform : loadDataInOut._uniforms )
            {
                const string rawName = uniform._name.substr( 0, uniform._name.find_first_of( "[" ) ).c_str();
                uniformBlock.append( Util::StringFormat( "\n#define {} {}.{}", rawName.c_str(), UNIFORM_BLOCK_NAME, rawName.c_str() ) );
            }

            const U8 layoutIndex = _context.renderAPI() == RenderAPI::Vulkan
                ? loadDataInOut._reflectionData._uniformBlockBindingIndex
                : ShaderProgram::GetGLBindingForDescriptorSlot( DescriptorSetUsage::PER_DRAW,
                                                                loadDataInOut._reflectionData._uniformBlockBindingIndex );

            Util::StringFormatTo( uniformBlock, uniformBlock.c_str(), layoutIndex, Util::StringFormat( "dvd_UniformBlock_{}", blockIndexInOut ) );

            previousUniformsInOut = loadDataInOut._uniforms;
        }

        string pushConstantCodeBlock{};
        if ( _context.renderAPI() == RenderAPI::Vulkan )
        {
            pushConstantCodeBlock =
                "layout( push_constant ) uniform constants\n"
                "{\n"
                "   mat4 data0;\n"
                "   mat4 data1;\n"
                "} PushConstants;\n"
                "#define PushData0 PushConstants.data0\n"
                "#define PushData1 PushConstants.data1";
        }
        else
        {
            pushConstantCodeBlock =
                "layout(location = 18) uniform mat4 PushConstantData[2];\n"
                "#define PushData0 PushConstantData[0]\n"
                "#define PushData1 PushConstantData[1]";
        }

        Util::ReplaceStringInPlace( loadDataInOut._sourceCodeGLSL, "//_PROGRAM_NAME_\\", Util::StringFormat("/*[ {} ]*/", loadDataInOut._shaderName.c_str()));
        Util::ReplaceStringInPlace( loadDataInOut._sourceCodeGLSL, "//_CUSTOM_UNIFORMS_\\", loadDataInOut._uniformBlock );
        Util::ReplaceStringInPlace( loadDataInOut._sourceCodeGLSL, "//_PUSH_CONSTANTS_DEFINE_\\", pushConstantCodeBlock );
    }

    void ShaderProgram::EraseAtom( const U64 atomHash )
    {
        // Clear the atom from the cache
        LockGuard<Mutex> w_lock( s_atomLock );
        EraseAtomLocked(atomHash);
    }

    void ShaderProgram::EraseAtomLocked( const U64 atomHash )
    {
        fixed_vector<U64, 128, true> queuedDeletion;

        s_atoms.erase( atomHash );

        for ( auto it = s_atomIncludes.cbegin(); it != s_atomIncludes.cend(); )
        {
            if ( it->first == atomHash)
            {
                it = s_atomIncludes.erase( it );
                continue;
            }

            if ( it->second.find( atomHash ) != it->second.cend() )
            {
                // Remove all atoms that included our target atom as well
                queuedDeletion.push_back( it->first );
            }
            ++it;
        }

        for (const U64 atom : queuedDeletion )
        {
            EraseAtomLocked(atom);
        }
    }

    void ShaderProgram::OnAtomChange( const std::string_view atomName, const FileUpdateEvent evt )
    {
        DIVIDE_GPU_ASSERT( evt != FileUpdateEvent::COUNT );

        // Do nothing if the specified file is "deleted". We do not want to break running programs
        // ADD and MODIFY events should get processed as usual
        if ( evt == FileUpdateEvent::DELETED )
        {
            return;
        }

        const U64 atomNameHash = _ID( string{ atomName }.c_str() );
        EraseAtomLocked(atomNameHash);

        //Get list of shader programs that use the atom and rebuild all shaders in list;
        SharedLock<SharedMutex> lock( s_programLock );
        for ( ShaderProgram* program : s_shaderPrograms )
        {
            DIVIDE_GPU_ASSERT( program != nullptr );

            for ( const U64 atomID : program->_usedAtomIDs )
            {
                if ( atomID == atomNameHash )
                {
                    s_recompileQueue.push( ShaderQueueEntry{ ._program = program } );
                    break;
                }
            }
        }
    }

};
