#include "stdafx.h"

#include "Headers/ShaderProgram.h"
#include "Headers/GLSLToSPIRV.h"

#include "Managers/Headers/SceneManager.h"

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

extern "C" {
#include "fcpp/fpp.h"
}

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable:4458)
#pragma warning(disable:4706)
#endif
#include <boost/regex.hpp>
#ifdef _MSC_VER
#pragma warning(pop)
#endif

#include <Vulkan/vulkan.hpp>

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

    constexpr U16 BYTE_BUFFER_VERSION = 1u;

    constexpr I8 s_maxHeaderRecursionLevel = 64;
    
    Mutex ShaderProgram::s_atomLock;
    Mutex ShaderProgram::g_cacheLock;
    ShaderProgram::AtomMap ShaderProgram::s_atoms;
    ShaderProgram::AtomInclusionMap ShaderProgram::s_atomIncludes;

    I64 ShaderProgram::s_shaderFileWatcherID = -1;
    ResourcePath ShaderProgram::shaderAtomLocationPrefix[to_base( ShaderType::COUNT ) + 1];
    U64 ShaderProgram::shaderAtomExtensionHash[to_base( ShaderType::COUNT ) + 1];
    Str8 ShaderProgram::shaderAtomExtensionName[to_base( ShaderType::COUNT ) + 1];

    ShaderProgram::ShaderQueue ShaderProgram::s_recompileQueue;
    ShaderProgram::ShaderQueue ShaderProgram::s_recompileFailedQueue;
    ShaderProgram::ShaderProgramMap ShaderProgram::s_shaderPrograms;
    ShaderProgram::LastRequestedShader ShaderProgram::s_lastRequestedShaderProgram = {};
    U8 ShaderProgram::k_commandBufferID = U8_MAX - MAX_BINDINGS_PER_DESCRIPTOR_SET;

    SharedMutex ShaderProgram::s_programLock;
    std::atomic_int ShaderProgram::s_shaderCount;

    ShaderProgram::BindingSetData ShaderProgram::s_bindingsPerSet;

    UpdateListener g_sFileWatcherListener(
        []( const std::string_view atomName, const FileUpdateEvent evt )
        {
            ShaderProgram::OnAtomChange( atomName, evt );
        }
    );

    namespace Preprocessor
    {
        struct WorkData
        {
            const char* _input = nullptr;
            size_t _inputSize = 0u;
            const char* _fileName = nullptr;

            std::array<char, 16 << 10> _scratch{};
            eastl::string _depends = "";
            eastl::string _default = "";
            eastl::string _output = "";

            U32 _scratchPos = 0u;
            U32 _fGetsPos = 0u;
            bool _firstError = true;
        };

        namespace Callback
        {
            FORCE_INLINE void AddDependency( const char* file, void* userData )
            {
                eastl::string& depends = static_cast<WorkData*>(userData)->_depends;

                depends += " \\\n ";
                depends += file;
            }

            char* Input( char* buffer, const int size, void* userData ) noexcept
            {
                WorkData* work = static_cast<WorkData*>(userData);
                int i = 0;
                for ( char ch = work->_input[work->_fGetsPos];
                      work->_fGetsPos < work->_inputSize && i < size - 1; ch = work->_input[++work->_fGetsPos] )
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

            char* Scratch( const char* str, WorkData& workData )
            {
                char* result = &workData._scratch[workData._scratchPos];
                strcpy( result, str );
                workData._scratchPos += to_U32( strlen( str ) ) + 1;
                return result;
            }

            void Error( void* userData, const char* format, const va_list args )
            {
                static bool firstErrorPrint = true;
                WorkData* work = static_cast<WorkData*>(userData);
                char formatted[1024];
                vsnprintf( formatted, 1024, format, args );
                if ( work->_firstError )
                {
                    work->_firstError = false;
                    Console::errorfn( "------------------------------------------" );
                    Console::errorfn( Locale::Get( _ID( "ERROR_GLSL_PARSE_ERROR_NAME_SHORT" ) ), work->_fileName );
                    firstErrorPrint = false;
                }
                if ( strlen( formatted ) != 1 && formatted[0] != '\n' )
                {
                    Console::errorfn( Locale::Get( _ID( "ERROR_GLSL_PARSE_ERROR_MSG" ) ), formatted );
                }
                else
                {
                    Console::errorfn( "------------------------------------------\n" );
                }
            }
        }

        eastl::string PreProcess( const eastl::string& source, const char* fileName )
        {
            constexpr U8 g_maxTagCount = 64;

            if ( source.empty() )
            {
                return source;
            }

            eastl::string temp( source.size() + 1, ' ' );
            {
                const char* in = source.data();
                char* out = temp.data();
                const char* end = out + source.size();

                for ( char ch = *in++; out < end && ch != '\0'; ch = *in++ )
                {
                    if ( ch != '\r' )
                    {
                        *out++ = ch;
                    }
                }
                *out = '\0';
            }

            WorkData workData{
                temp.c_str(), // input
                temp.size(),  // input size
                fileName      // file name
            };

            fppTag tags[g_maxTagCount]{};
            fppTag* tagHead = tags;

            const auto setTag = [&tagHead]( const int tag, void* value )
            {
                tagHead->tag = tag;
                tagHead->data = value;
                ++tagHead;
            };

            setTag( FPPTAG_USERDATA, &workData );
            setTag( FPPTAG_DEPENDS, Callback::AddDependency );
            setTag( FPPTAG_INPUT, Callback::Input );
            setTag( FPPTAG_OUTPUT, Callback::Output );
            setTag( FPPTAG_ERROR, Callback::Error );
            setTag( FPPTAG_INPUT_NAME, Callback::Scratch( fileName, workData ) );
            setTag( FPPTAG_KEEPCOMMENTS, (void*)TRUE );
            setTag( FPPTAG_IGNOREVERSION, (void*)FALSE );
            setTag( FPPTAG_LINE, (void*)FALSE );
            setTag( FPPTAG_OUTPUTBALANCE, (void*)TRUE );
            setTag( FPPTAG_OUTPUTSPACE, (void*)TRUE );
            setTag( FPPTAG_NESTED_COMMENTS, (void*)TRUE );
            //setTag(FPPTAG_IGNORE_CPLUSPLUS, (void*)TRUE);
            setTag( FPPTAG_RIGHTCONCAT, (void*)TRUE );
            //setTag(FPPTAG_WARNILLEGALCPP,   (void*)TRUE);
            setTag( FPPTAG_END, nullptr );

            if ( fppPreProcess( tags ) != 0 )
            {
                NOP();
            }

            return workData._output;
        }

    } //Preprocessor

    namespace
    {
        U64 s_newestShaderAtomWriteTime = 0u; //< Used to detect modified shader atoms to validate/invalidate shader cache
        bool s_useShaderCache = true;
        bool s_targetVulkan = false;

        constexpr U8 s_reserverdTextureSlotsPerDraw = to_base( TextureSlot::COUNT ) + 2; /*{Reflection + Refraction}*/
        constexpr U8 s_reserverdUBOSlotsPerDraw = 0u;
        // One pair of each: { globalIndexCount, lightClusterAABB}, {histogram buffer}, {bone transforms}
        constexpr U8 s_reserverdSSBOSlotsPerDraw = 3u; 
        constexpr U8 s_reserverdBufferSlotsPerDraw = s_reserverdUBOSlotsPerDraw + s_reserverdSSBOSlotsPerDraw;

        // One of each: HiZ Output, Irradiance Target, LUT Target, PreFiltered target, Luminance Input, Luminance Average Target
        constexpr U8 s_reservedImageSlotsPerDraw = MAX_BINDINGS_PER_DESCRIPTOR_SET - s_reserverdBufferSlotsPerDraw - s_reserverdUBOSlotsPerDraw; 

        U8 s_textureSlot = s_reserverdTextureSlotsPerDraw;
        U8 s_bufferSlot = s_textureSlot + s_reserverdBufferSlotsPerDraw;
        U8 s_imageSlot = s_reservedImageSlotsPerDraw;

        [[nodiscard]] size_t DefinesHash( const ModuleDefines& defines ) noexcept
        {
            if ( defines.empty() )
            {
                return 0u;
            }

            size_t hash = 7919;
            for ( const auto& [defineString, appendPrefix] : defines )
            {
                Util::Hash_combine( hash, _ID( defineString.c_str() ) );
                Util::Hash_combine( hash, appendPrefix );
            }
            return hash;
        }

        [[nodiscard]] ResourcePath ShaderAPILocation()
        {
            return (s_targetVulkan ? Paths::Shaders::g_cacheLocationVK : Paths::Shaders::g_cacheLocationGL);
        }

        [[nodiscard]] ResourcePath ShaderBuildCacheLocation()
        {
            return Paths::g_cacheLocation + Paths::Shaders::g_cacheLocation + Paths::g_buildTypeLocation;
        }

        [[nodiscard]] ResourcePath ShaderParentCacheLocation()
        {
            return ShaderBuildCacheLocation() + ShaderAPILocation();
        }

        [[nodiscard]] ResourcePath SpvCacheLocation()
        {
            return ShaderParentCacheLocation() + Paths::Shaders::g_cacheLocationSpv;
        }

        [[nodiscard]] ResourcePath ReflCacheLocation()
        {
            return ShaderParentCacheLocation() + Paths::Shaders::g_cacheLocationRefl;
        }

        [[nodiscard]] ResourcePath TxtCacheLocation()
        {
            return ShaderParentCacheLocation() + Paths::Shaders::g_cacheLocationText;
        }

        [[nodiscard]] ResourcePath SpvTargetName( const Str256& fileName )
        {
            return ResourcePath{ fileName + "." + Paths::Shaders::g_SPIRVExt };
        }

        [[nodiscard]] ResourcePath ReflTargetName( const Str256& fileName )
        {
            return ResourcePath{ fileName + "." + Paths::Shaders::g_ReflectionExt };
        }

        [[nodiscard]] bool ValidateCacheLocked( const ShaderProgram::LoadData::ShaderCacheType type, const Str256& sourceFileName, const Str256& fileName )
        {
            if ( !s_useShaderCache )
            {
                return false;
            }

            //"There are only two hard things in Computer Science: cache invalidation and naming things" - Phil Karlton
            //"There are two hard things in computer science: cache invalidation, naming things, and off-by-one errors." - Leon Bambrick

            // Get our source file's "last written" timestamp. Every cache file that's older than this is automatically out-of-date
            U64 lastWriteTime = 0u, lastWriteTimeCache = 0u;
            const ResourcePath sourceShaderFullPath = Paths::g_assetsLocation + Paths::g_shadersLocation + Paths::Shaders::GLSL::g_GLSLShaderLoc + sourceFileName;
            if ( fileLastWriteTime( sourceShaderFullPath, lastWriteTime ) != FileError::NONE )
            {
                return false;
            }

            ResourcePath filePath;
            switch ( type )
            {
                case ShaderProgram::LoadData::ShaderCacheType::REFLECTION: filePath = ReflCacheLocation() + ReflTargetName( fileName ); break;
                case ShaderProgram::LoadData::ShaderCacheType::GLSL: filePath = TxtCacheLocation() + ResourcePath{ fileName }; break;
                case ShaderProgram::LoadData::ShaderCacheType::SPIRV: filePath = SpvCacheLocation() + SpvTargetName( fileName ); break;
                default: return false;
            }

            if ( fileLastWriteTime( filePath, lastWriteTimeCache ) != FileError::NONE ||
                 lastWriteTimeCache < lastWriteTime ||
                 lastWriteTimeCache < s_newestShaderAtomWriteTime )
            {
                return false;
            }

            return true;
        }
        [[nodiscard]] bool ValidateCache( const ShaderProgram::LoadData::ShaderCacheType type, const Str256& sourceFileName, const Str256& fileName )
        {
            LockGuard<Mutex> rw_lock( ShaderProgram::g_cacheLock );
            return ValidateCache( type, sourceFileName, fileName );
        }

        [[nodiscard]] bool DeleteCacheLocked( const ShaderProgram::LoadData::ShaderCacheType type, const Str256& fileName )
        {
            FileError err = FileError::NONE;
            switch ( type )
            {
                case ShaderProgram::LoadData::ShaderCacheType::REFLECTION: err = deleteFile( ReflCacheLocation(), ReflTargetName( fileName ) ); break;
                case ShaderProgram::LoadData::ShaderCacheType::GLSL: err = deleteFile( TxtCacheLocation(), ResourcePath{ fileName } ); break;
                case ShaderProgram::LoadData::ShaderCacheType::SPIRV: err = deleteFile( SpvCacheLocation(), SpvTargetName( fileName ) ); break;
                default: return false;
            }

            return err == FileError::NONE || err == FileError::FILE_NOT_FOUND;
        }

        [[nodiscard]] bool DeleteCache( const ShaderProgram::LoadData::ShaderCacheType type, const Str256& fileName )
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

    bool InitGLSW( const GFXDevice& gfx, const DeviceInformation& deviceInfo, const Configuration& config )
    {
        const RenderAPI renderingAPI = gfx.renderAPI();

        const auto AppendToShaderHeader = []( const ShaderType type, const string& entry )
        {
            glswAddDirectiveToken( type != ShaderType::COUNT ? Names::shaderTypes[to_U8( type )] : "", entry.c_str() );
        };

        const auto AppendResourceBindingSlots = [&AppendToShaderHeader, &gfx]( const bool targetOpenGL )
        {

            if ( targetOpenGL )
            {
                const auto AppendSetBindings = [&]( const DescriptorSetUsage setUsage )
                {
                    for ( U8 i = 0u; i < MAX_BINDINGS_PER_DESCRIPTOR_SET; ++i )
                    {
                        const U8 glSlot = ShaderProgram::GetGLBindingForDescriptorSlot( setUsage, i );
                        AppendToShaderHeader( ShaderType::COUNT, Util::StringFormat( "#define %s_%d %d",
                                                                                     TypeUtil::DescriptorSetUsageToString( setUsage ),
                                                                                     i,
                                                                                     glSlot ).c_str() );
                    }
                };

                AppendSetBindings( DescriptorSetUsage::PER_DRAW );
                AppendSetBindings( DescriptorSetUsage::PER_BATCH );
                AppendSetBindings( DescriptorSetUsage::PER_PASS );
                AppendSetBindings( DescriptorSetUsage::PER_FRAME );

                AppendToShaderHeader( ShaderType::VERTEX, Util::StringFormat( "#define PER_DRAW_BONE_BUFFER_BINDING %d", ShaderProgram::BONE_BUFFER_BINDING_SLOT ) );

                AppendToShaderHeader( ShaderType::COUNT, "#define DESCRIPTOR_SET_RESOURCE(SET, BINDING) layout(binding = CONCATENATE(SET, BINDING))" );
                AppendToShaderHeader( ShaderType::COUNT, "#define DESCRIPTOR_SET_RESOURCE_OFFSET(SET, BINDING, OFFSET) layout(binding = CONCATENATE(SET, BINDING), offset = OFFSET)" );
                AppendToShaderHeader( ShaderType::COUNT, "#define DESCRIPTOR_SET_RESOURCE_LAYOUT(SET, BINDING, LAYOUT) layout(binding = CONCATENATE(SET, BINDING), LAYOUT)" );
                AppendToShaderHeader( ShaderType::COUNT, "#define DESCRIPTOR_SET_RESOURCE_OFFSET_LAYOUT(SET, BINDING, OFFSET, LAYOUT) layout(binding = CONCATENATE(SET, BINDING), offset = OFFSET, LAYOUT)" );
            }
            else
            {
                AppendToShaderHeader( ShaderType::VERTEX, Util::StringFormat( "#define BONE_BUFFER_BINDING %d", ShaderProgram::BONE_BUFFER_BINDING_SLOT ) );

                for ( U8 i = 0u; i < to_base( DescriptorSetUsage::COUNT ); ++i )
                {
                    AppendToShaderHeader( ShaderType::COUNT, Util::StringFormat( "#define %s %d", TypeUtil::DescriptorSetUsageToString( static_cast<DescriptorSetUsage>(i) ), i ).c_str() );
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
            string baseString = "     _out.%s = _in[index].%s;";
            if ( type == ShaderType::TESSELLATION_CTRL )
            {
                baseString = "    _out[gl_InvocationID].%s = _in[index].%s;";
            }

            string passData( "void PassData(in int index) {" );
            passData.append( "\n" );
            for ( const auto& [varType, name] : shaderVaryings )
            {
                passData.append( Util::StringFormat( baseString.c_str(), name, name ) );
                passData.append( "\n" );
            }

            passData.append( "#if defined(HAS_VELOCITY)\n" );
            passData.append( Util::StringFormat( baseString.c_str(), "_prevVertexWVP", "_prevVertexWVP" ) );
            passData.append( "\n#endif //HAS_VELOCITY\n" );

            passData.append( "#if defined(ENABLE_TBN)\n" );
            passData.append( Util::StringFormat( baseString.c_str(), "_tbnWV", "_tbnWV" ) );
            passData.append( "\n#endif //ENABLE_TBN\n" );

            passData.append( "}\n" );

            return passData;
        };

        const auto addVaryings = [&]( const ShaderType type )
        {
            for ( const auto& [varType, name] : shaderVaryings )
            {
                AppendToShaderHeader( type, Util::StringFormat( "    %s %s;", varType, name ) );
            }
            AppendToShaderHeader( type, "#if defined(HAS_VELOCITY)" );
            AppendToShaderHeader( type, "    vec4 _prevVertexWVP;" );
            AppendToShaderHeader( type, "#endif //HAS_VELOCITY" );
            
            AppendToShaderHeader( type, "#if defined(ENABLE_TBN)" );
            AppendToShaderHeader( type, "    mat3 _tbnWV;" );
            AppendToShaderHeader( type, "#endif //ENABLE_TBN" );
        };

        // Initialize GLSW
        I32 glswState = -1;
        if ( !glswGetCurrentContext() )
        {
            glswState = glswInit();
            DIVIDE_ASSERT( glswState == 1 );
        }

        const U16 reflectionProbeRes = to_U16( nextPOW2( CLAMPED( to_U32( config.rendering.reflectionProbeResolution ), 16u, 4096u ) - 1u ) );

        static_assert(Config::MAX_BONE_COUNT_PER_NODE <= 1024, "ShaderProgram error: too many bones per vert. Can't fit inside UBO");

        // Add our engine specific defines and various code pieces to every GLSL shader
        // Add version as the first shader statement, followed by copyright notice
        AppendToShaderHeader( ShaderType::COUNT, renderingAPI == RenderAPI::OpenGL ? "#version 460 core" : "#version 450" );
        AppendToShaderHeader( ShaderType::COUNT, "//_PROGRAM_NAME_\\" );
        AppendToShaderHeader( ShaderType::COUNT, "/*Copyright 2009-2022 DIVIDE-Studio*/" );

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
            AppendToShaderHeader( ShaderType::COUNT, Util::StringFormat( "#define TEX_%s %d", TypeUtil::TextureOperationToString( static_cast<TextureOperation>(i) ), i ).c_str() );
        }
        AppendToShaderHeader( ShaderType::COUNT, Util::StringFormat( "#define WORLD_X_AXIS vec3(%1.1f,%1.1f,%1.1f)", WORLD_X_AXIS.x, WORLD_X_AXIS.y, WORLD_X_AXIS.z ) );
        AppendToShaderHeader( ShaderType::COUNT, Util::StringFormat( "#define WORLD_Y_AXIS vec3(%1.1f,%1.1f,%1.1f)", WORLD_Y_AXIS.x, WORLD_Y_AXIS.y, WORLD_Y_AXIS.z ) );
        AppendToShaderHeader( ShaderType::COUNT, Util::StringFormat( "#define WORLD_Z_AXIS vec3(%1.1f,%1.1f,%1.1f)", WORLD_Z_AXIS.x, WORLD_Z_AXIS.y, WORLD_Z_AXIS.z ) );


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
        AppendToShaderHeader( ShaderType::COUNT, "#define ATTRIB_FREE_START 12" );
        AppendToShaderHeader( ShaderType::FRAGMENT, "#define MAX_SHININESS " + Util::to_string( Material::MAX_SHININESS ) );

        const string interfaceLocationString = "layout(location = 0) ";

        for ( U8 i = 0u; i < to_U8( ShadingMode::COUNT ) + 1u; ++i )
        {
            const ShadingMode mode = static_cast<ShadingMode>(i);
            AppendToShaderHeader( ShaderType::FRAGMENT, Util::StringFormat( "#define SHADING_%s %d", TypeUtil::ShadingModeToString( mode ), i ) );
        }

        AppendToShaderHeader( ShaderType::FRAGMENT, Util::StringFormat( "#define SHADING_COUNT %d", to_base( ShadingMode::COUNT ) ) );

        for ( U8 i = 0u; i < to_U8( MaterialDebugFlag::COUNT ) + 1u; ++i )
        {
            const MaterialDebugFlag flag = static_cast<MaterialDebugFlag>(i);
            AppendToShaderHeader( ShaderType::FRAGMENT, Util::StringFormat( "#define DEBUG_%s %d", TypeUtil::MaterialDebugFlagToString( flag ), i ) );
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

    ModuleDefine::ModuleDefine( const char* define, const bool addPrefix )
        : ModuleDefine( string{ define }, addPrefix )
    {
    }

    ModuleDefine::ModuleDefine( const string& define, const bool addPrefix )
        : _define( define ),
        _addPrefix( addPrefix )
    {
    }

    ShaderModuleDescriptor::ShaderModuleDescriptor( ShaderType type, const Str64& file, const Str64& variant )
        : _moduleType( type ), _sourceFile( file ), _variant( variant )
    {
    }

    ShaderProgramDescriptor::ShaderProgramDescriptor() noexcept
        : PropertyDescriptor( DescriptorType::DESCRIPTOR_SHADER )
    {
    }

    size_t ShaderProgramDescriptor::getHash() const noexcept
    {
        _hash = PropertyDescriptor::getHash();
        for ( const ShaderModuleDescriptor& desc : _modules )
        {
            Util::Hash_combine( _hash, DefinesHash( desc._defines ),
                                std::string( desc._variant.c_str() ),
                                desc._sourceFile.data(),
                                desc._moduleType );
        }

        return _hash;
    }

    bool operator==( const ShaderProgramMapEntry& lhs, const ShaderProgramMapEntry& rhs ) noexcept
    {
        return lhs._generation == rhs._generation &&
               lhs._program == rhs._program;
    }

    bool operator!=( const ShaderProgramMapEntry& lhs, const ShaderProgramMapEntry& rhs ) noexcept
    {
        return lhs._generation != rhs._generation ||
               lhs._program != rhs._program;
    }

    std::atomic_bool ShaderModule::s_modulesRemoved;
    SharedMutex ShaderModule::s_shaderNameLock;
    ShaderModule::ShaderMap ShaderModule::s_shaderNameMap;

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
                    Console::warnfn(Locale::Get(_ID("SHADER_MODULE_EXPIRED")), shaderModule->_name.c_str());
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

    ShaderModule* ShaderModule::GetShader( const Str256& name )
    {
        SharedLock<SharedMutex> r_lock( s_shaderNameLock );
        return GetShaderLocked( name );
    }

    ShaderModule* ShaderModule::GetShaderLocked( const Str256& name )
    {
        // Try to find the shader
        const ShaderMap::iterator it = s_shaderNameMap.find( _ID( name.c_str() ) );
        if ( it != std::end( s_shaderNameMap ) )
        {
            return it->second.get();
        }

        return nullptr;
    }

    ShaderModule::ShaderModule( GFXDevice& context, const Str256& name, const U32 generation )
        : GUIDWrapper()
        , GraphicsResource( context, Type::SHADER, getGUID(), _ID( name.c_str() ) )
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

    ShaderProgram::ShaderProgram( GFXDevice& context,
                                  const size_t descriptorHash,
                                  const Str256& shaderName,
                                  const Str256& shaderFileName,
                                  const ResourcePath& shaderFileLocation,
                                  ShaderProgramDescriptor descriptor,
                                  ResourceCache& parentCache )
        : CachedResource( ResourceType::GPU_OBJECT, descriptorHash, shaderName, ResourcePath( shaderFileName ), shaderFileLocation ),
        GraphicsResource( context, Type::SHADER_PROGRAM, getGUID(), _ID( shaderName.c_str() ) ),
        _descriptor( MOV( descriptor ) ),
        _parentCache( parentCache )
    {
        if ( shaderFileName.empty() )
        {
            assetName( ResourcePath( resourceName().c_str() ) );
        }
        s_shaderCount.fetch_add( 1, std::memory_order_relaxed );
    }

    ShaderProgram::~ShaderProgram()
    {
        _parentCache.remove( this );
        Console::d_printfn( Locale::Get( _ID( "SHADER_PROGRAM_REMOVE" ) ), resourceName().c_str() );
        s_shaderCount.fetch_sub( 1, std::memory_order_relaxed );
    }

    bool ShaderProgram::load()
    {
        Start( *CreateTask( [this]( const Task& )
                            {
                                PROFILE_SCOPE_AUTO( Profiler::Category::Streaming );

                                hashMap<U64, PerFileShaderData> loadDataByFile{};
                                loadInternal( loadDataByFile, false );
                                RegisterShaderProgram( this );
                                CachedResource::load();
                            } ),
               _context.context().taskPool( TaskPoolType::HIGH_PRIORITY ) );

        return true;
    }

    bool ShaderProgram::unload()
    {
        // Our GPU Arena will clean up the memory, but we should still destroy these
        _uniformBlockBuffers.clear();
        // Unregister the program from the manager
        if ( UnregisterShaderProgram( handle() ) )
        {
            handle( SHADER_INVALID_HANDLE );
        }

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

    void ShaderProgram::Idle( PlatformContext& platformContext, const bool fast )
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
                Console::errorfn( Locale::Get( _ID( "ERROR_SHADER_RECOMPILE_FAILED" ) ), entry._program->resourceName().c_str() );

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
    bool ShaderProgram::RecompileShaderProgram( const Str256& name )
    {
        bool state = false;

        SharedLock<SharedMutex> lock( s_programLock );

        // Find the shader program
        for ( const ShaderProgramMapEntry& entry : s_shaderPrograms )
        {

            ShaderProgram* program = entry._program;
            assert( program != nullptr );

            const Str256& shaderName = program->resourceName();
            // Check if the name matches any of the program's name components    
            if ( shaderName.find( name ) != Str256::npos || shaderName.compare( name ) == 0 )
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
            Console::errorfn( Locale::Get( _ID( "ERROR_RECOMPILE_NOT_FOUND" ) ), name.c_str() );
        }

        return state;
    }

    ErrorCode ShaderProgram::OnStartup( ResourceCache* parentCache )
    {
        if constexpr ( !Config::Build::IS_SHIPPING_BUILD )
        {
            FileWatcher& watcher = FileWatcherManager::allocateWatcher();
            s_shaderFileWatcherID = watcher.getGUID();
            g_sFileWatcherListener.addIgnoredEndCharacter( '~' );
            g_sFileWatcherListener.addIgnoredExtension( "tmp" );

            const vector<ResourcePath> atomLocations = GetAllAtomLocations();
            for ( const ResourcePath& loc : atomLocations )
            {
                if ( !CreateDirectories( loc ) )
                {
                    DIVIDE_UNEXPECTED_CALL();
                }
                watcher().addWatch( loc.c_str(), &g_sFileWatcherListener );
            }
        }

        for ( U8 i = 0u; i < MAX_BINDINGS_PER_DESCRIPTOR_SET; ++i )
        {
            auto& data = s_bindingsPerSet[to_base( DescriptorSetUsage::PER_DRAW )][i];
            data._glBinding = i;
            data._visibility = to_base(ShaderStageVisibility::ALL);
            data._type = DescriptorSetBindingType::COUNT;
        }

        const ResourcePath locPrefix{ Paths::g_assetsLocation + Paths::g_shadersLocation + Paths::Shaders::GLSL::g_GLSLShaderLoc };

        shaderAtomLocationPrefix[to_base( ShaderType::FRAGMENT )] = locPrefix + Paths::Shaders::GLSL::g_fragAtomLoc;
        shaderAtomLocationPrefix[to_base( ShaderType::VERTEX )] = locPrefix + Paths::Shaders::GLSL::g_vertAtomLoc;
        shaderAtomLocationPrefix[to_base( ShaderType::GEOMETRY )] = locPrefix + Paths::Shaders::GLSL::g_geomAtomLoc;
        shaderAtomLocationPrefix[to_base( ShaderType::TESSELLATION_CTRL )] = locPrefix + Paths::Shaders::GLSL::g_tescAtomLoc;
        shaderAtomLocationPrefix[to_base( ShaderType::TESSELLATION_EVAL )] = locPrefix + Paths::Shaders::GLSL::g_teseAtomLoc;
        shaderAtomLocationPrefix[to_base( ShaderType::COMPUTE )] = locPrefix + Paths::Shaders::GLSL::g_compAtomLoc;
        shaderAtomLocationPrefix[to_base( ShaderType::COUNT )] = locPrefix + Paths::Shaders::GLSL::g_comnAtomLoc;

        shaderAtomExtensionName[to_base( ShaderType::FRAGMENT )] = Paths::Shaders::GLSL::g_fragAtomExt;
        shaderAtomExtensionName[to_base( ShaderType::VERTEX )] = Paths::Shaders::GLSL::g_vertAtomExt;
        shaderAtomExtensionName[to_base( ShaderType::GEOMETRY )] = Paths::Shaders::GLSL::g_geomAtomExt;
        shaderAtomExtensionName[to_base( ShaderType::TESSELLATION_CTRL )] = Paths::Shaders::GLSL::g_tescAtomExt;
        shaderAtomExtensionName[to_base( ShaderType::TESSELLATION_EVAL )] = Paths::Shaders::GLSL::g_teseAtomExt;
        shaderAtomExtensionName[to_base( ShaderType::COMPUTE )] = Paths::Shaders::GLSL::g_compAtomExt;
        shaderAtomExtensionName[to_base( ShaderType::COUNT )] = "." + Paths::Shaders::GLSL::g_comnAtomExt;

        for ( U8 i = 0u; i < to_base( ShaderType::COUNT ) + 1; ++i )
        {
            shaderAtomExtensionHash[i] = _ID( shaderAtomExtensionName[i].c_str() );
        }

        const PlatformContext& ctx = parentCache->context();
        const Configuration& config = ctx.config();
        s_useShaderCache = config.debug.useShaderCache;
        s_targetVulkan = ctx.gfx().renderAPI() == RenderAPI::Vulkan;

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

        if ( !InitGLSW( ctx.gfx(), GFXDevice::GetDeviceInformation(), config ) )
        {
            return ErrorCode::GLSL_INIT_ERROR;
        }

        SpirvHelper::Init();

        s_textureSlot = s_reserverdTextureSlotsPerDraw;
        s_bufferSlot = s_textureSlot + s_reserverdBufferSlotsPerDraw;
        s_imageSlot = s_reservedImageSlotsPerDraw;

        return ErrorCode::NO_ERR;
    }

    bool ShaderProgram::OnShutdown()
    {
        SpirvHelper::Finalize();

        while ( !s_recompileQueue.empty() )
        {
            s_recompileQueue.pop();
        }
        s_shaderPrograms.fill( {} );
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
        DIVIDE_ASSERT( usage != DescriptorSetUsage::PER_DRAW );

        DIVIDE_ASSERT( slot < MAX_BINDINGS_PER_DESCRIPTOR_SET );

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
                    bindingData._glBinding = s_bufferSlot++;
                }
                break;
            default:
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
                case DescriptorSetBindingType::COMBINED_IMAGE_SAMPLER: count = s_reserverdTextureSlotsPerDraw; break;
                case DescriptorSetBindingType::IMAGE: count = s_reservedImageSlotsPerDraw; break;
                case DescriptorSetBindingType::UNIFORM_BUFFER: count = s_reserverdUBOSlotsPerDraw; break;
                case DescriptorSetBindingType::SHADER_STORAGE_BUFFER: count = s_reserverdSSBOSlotsPerDraw; break;
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
    bool ShaderProgram::OnThreadCreated( const GFXDevice& gfx, [[maybe_unused]] const std::thread::id& threadID )
    {
        return InitGLSW( gfx, GFXDevice::GetDeviceInformation(), gfx.context().config() );
    }

    void ShaderProgram::OnEndFrame( GFXDevice& gfx )
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Graphics );

        size_t totalUniformBufferSize = 0u;
        SharedLock<SharedMutex> lock( s_programLock );
        for ( const ShaderProgramMapEntry& entry : s_shaderPrograms )
        {
            if ( entry._program != nullptr )
            {
                for ( UniformBlockUploader& block : entry._program->_uniformBlockBuffers )
                {
                    block.onFrameEnd();
                    totalUniformBufferSize += block.totalBufferSize();
                }
            }
        }
        gfx.getPerformanceMetrics()._uniformBufferVRAMUsage = totalUniformBufferSize;
    }

    /// Whenever a new program is created, it's registered with the manager
    void ShaderProgram::RegisterShaderProgram( ShaderProgram* shaderProgram )
    {
        const auto cleanOldShaders = []()
        {
            DIVIDE_UNEXPECTED_CALL_MSG( "Not Implemented!" );
        };

        assert( shaderProgram != nullptr );

        LockGuard<SharedMutex> lock( s_programLock );
        if ( shaderProgram->handle() != SHADER_INVALID_HANDLE )
        {
            const ShaderProgramMapEntry& existingEntry = s_shaderPrograms[shaderProgram->handle()._id];
            if ( existingEntry._generation == shaderProgram->handle()._generation )
            {
                // Nothing to do. Probably a reload of some kind.
                assert( existingEntry._program != nullptr && existingEntry._program->getGUID() == shaderProgram->getGUID() );
                return;
            }
        }

        U16 idx = 0u;
        bool retry = false;
        for ( ShaderProgramMapEntry& entry : s_shaderPrograms )
        {
            if ( entry._program == nullptr && entry._generation < U8_MAX )
            {
                entry._program = shaderProgram;
                shaderProgram->_handle = { idx, entry._generation };
                return;
            }
            if ( ++idx == 0u )
            {
                if ( retry )
                {
                    // We only retry once!
                    DIVIDE_UNEXPECTED_CALL();
                }
                retry = true;
                // Handle overflow
                cleanOldShaders();
            }
        }
    }

    /// Unloading/Deleting a program will unregister it from the manager
    bool ShaderProgram::UnregisterShaderProgram( const ShaderProgramHandle shaderHandle )
    {
        if ( shaderHandle != SHADER_INVALID_HANDLE )
        {
            LockGuard<SharedMutex> lock( s_programLock );
            ShaderProgramMapEntry& entry = s_shaderPrograms[shaderHandle._id];

            if ( entry._generation == shaderHandle._generation )
            {
                if ( entry._program && entry._program == s_lastRequestedShaderProgram._program )
                {
                    s_lastRequestedShaderProgram = {};
                }
                entry._program = nullptr;
                if ( entry._generation < U8_MAX )
                {
                    entry._generation += 1u;
                }
                return true;
            }
        }

        // application shutdown?
        return false;
    }

    ShaderProgram* ShaderProgram::FindShaderProgram( const ShaderProgramHandle shaderHandle )
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Graphics );

        SharedLock<SharedMutex> lock( s_programLock );

        if ( shaderHandle == s_lastRequestedShaderProgram._handle )
        {
            return s_lastRequestedShaderProgram._program;
        }

        assert( shaderHandle._id != U16_MAX && shaderHandle._generation != U8_MAX );
        const ShaderProgramMapEntry& entry = s_shaderPrograms[shaderHandle._id];
        if ( entry._generation == shaderHandle._generation )
        {
            s_lastRequestedShaderProgram = { entry._program, shaderHandle };

            return entry._program;
        }

        s_lastRequestedShaderProgram = {};
        return nullptr;
    }

    void ShaderProgram::RebuildAllShaders()
    {
        SharedLock<SharedMutex> lock( s_programLock );
        for ( const ShaderProgramMapEntry& entry : s_shaderPrograms )
        {
            if ( entry._program != nullptr )
            {
                s_recompileQueue.push( ShaderQueueEntry{ ._program = entry._program } );
            }
        }
    }

    vector<ResourcePath> ShaderProgram::GetAllAtomLocations()
    {
        static vector<ResourcePath> atomLocations;
        if ( atomLocations.empty() )
        {
            // General
            atomLocations.emplace_back( Paths::g_assetsLocation +
                                        Paths::g_shadersLocation );
            // GLSL
            atomLocations.emplace_back( Paths::g_assetsLocation +
                                        Paths::g_shadersLocation +
                                        Paths::Shaders::GLSL::g_GLSLShaderLoc );

            atomLocations.emplace_back( Paths::g_assetsLocation +
                                        Paths::g_shadersLocation +
                                        Paths::Shaders::GLSL::g_GLSLShaderLoc +
                                        Paths::Shaders::GLSL::g_comnAtomLoc );

            atomLocations.emplace_back( Paths::g_assetsLocation +
                                        Paths::g_shadersLocation +
                                        Paths::Shaders::GLSL::g_GLSLShaderLoc +
                                        Paths::Shaders::GLSL::g_compAtomLoc );

            atomLocations.emplace_back( Paths::g_assetsLocation +
                                        Paths::g_shadersLocation +
                                        Paths::Shaders::GLSL::g_GLSLShaderLoc +
                                        Paths::Shaders::GLSL::g_fragAtomLoc );

            atomLocations.emplace_back( Paths::g_assetsLocation +
                                        Paths::g_shadersLocation +
                                        Paths::Shaders::GLSL::g_GLSLShaderLoc +
                                        Paths::Shaders::GLSL::g_geomAtomLoc );

            atomLocations.emplace_back( Paths::g_assetsLocation +
                                        Paths::g_shadersLocation +
                                        Paths::Shaders::GLSL::g_GLSLShaderLoc +
                                        Paths::Shaders::GLSL::g_tescAtomLoc );

            atomLocations.emplace_back( Paths::g_assetsLocation +
                                        Paths::g_shadersLocation +
                                        Paths::Shaders::GLSL::g_GLSLShaderLoc +
                                        Paths::Shaders::GLSL::g_teseAtomLoc );

            atomLocations.emplace_back( Paths::g_assetsLocation +
                                        Paths::g_shadersLocation +
                                        Paths::Shaders::GLSL::g_GLSLShaderLoc +
                                        Paths::Shaders::GLSL::g_vertAtomLoc );
        }

        return atomLocations;
    }

    const eastl::string& ShaderProgram::ShaderFileRead( const ResourcePath& filePath, const ResourcePath& atomName, const bool recurse, eastl::set<U64>& foundAtomIDsInOut, bool& wasParsed )
    {
        LockGuard<Mutex> w_lock( s_atomLock );
        return ShaderFileReadLocked( filePath, atomName, recurse, foundAtomIDsInOut, wasParsed );
    }

    eastl::string ShaderProgram::PreprocessIncludes( const ResourcePath& name,
                                                     const eastl::string& source,
                                                     const I32 level,
                                                     eastl::set<U64>& foundAtomIDsInOut,
                                                     const bool lock )
    {
        if ( level > s_maxHeaderRecursionLevel )
        {
            Console::errorfn( Locale::Get( _ID( "ERROR_GLSL_INCLUD_LIMIT" ) ) );
        }

        size_t lineNumber = 1;
        boost::smatch matches;

        string line;
        eastl::string output, includeString;
        istringstream input( source.c_str() );

        const boost::regex searchPatern = boost::regex{ Paths::g_includePattern.c_str() };

        while ( std::getline( input, line ) )
        {
            const std::string_view directive = !line.empty() ? std::string_view{ line }.substr( 1 ) : "";

            const bool isInclude = Util::BeginsWith( line, "#", true ) &&
                !Util::BeginsWith( directive, "version", true ) &&
                !Util::BeginsWith( directive, "extension", true ) &&
                !Util::BeginsWith( directive, "define", true ) &&
                !Util::BeginsWith( directive, "if", true ) &&
                !Util::BeginsWith( directive, "else", true ) &&
                !Util::BeginsWith( directive, "elif", true ) &&
                !Util::BeginsWith( directive, "endif", true ) &&
                !Util::BeginsWith( directive, "pragma", true ) &&
                boost::regex_search( line, matches, searchPatern );
            if ( !isInclude )
            {
                output.append( line.c_str() );
            }
            else
            {
                const ResourcePath includeFile = ResourcePath( Util::Trim( matches[1].str() ) );
                foundAtomIDsInOut.insert( _ID( includeFile.c_str() ) );

                ShaderType typeIndex = ShaderType::COUNT;
                bool found = false;
                // switch will throw warnings due to promotion to int
                const U64 extHash = _ID( Util::GetTrailingCharacters( includeFile.str(), 4 ).c_str() );
                for ( U8 i = 0; i < to_base( ShaderType::COUNT ) + 1; ++i )
                {
                    if ( extHash == shaderAtomExtensionHash[i] )
                    {
                        typeIndex = static_cast<ShaderType>(i);
                        found = true;
                        break;
                    }
                }

                DIVIDE_ASSERT( found, "Invalid shader include type" );
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
                    Console::errorfn( Locale::Get( _ID( "ERROR_GLSL_NO_INCLUDE_FILE" ) ), name.c_str(), lineNumber, includeFile.c_str() );
                }
                if ( wasParsed )
                {
                    output.append( includeString );
                }
                else
                {
                    output.append( PreprocessIncludes( name, includeString, level + 1, foundAtomIDsInOut, lock ) );
                }
            }

            output.append( "\n" );
            ++lineNumber;
        }

        return output;
    }

    /// Open the file found at 'filePath' matching 'atomName' and return it's source code
    const eastl::string& ShaderProgram::ShaderFileReadLocked( const ResourcePath& filePath, const ResourcePath& atomName, const bool recurse, eastl::set<U64>& foundAtomIDsInOut, bool& wasParsed )
    {
        const U64 atomNameHash = _ID( atomName.c_str() );
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
        eastl::string& output = s_atoms[atomNameHash];
        output.clear();
        eastl::set<U64>& atoms = s_atomIncludes[atomNameHash];
        atoms.clear();

        if ( readFile( filePath, atomName, output, FileType::TEXT ) != FileError::NONE )
        {
            DIVIDE_UNEXPECTED_CALL();
        }
        if ( recurse )
        {
            output = PreprocessIncludes( atomName, output, 0, atoms, false );
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
                                         ResourcePath( dataIn._shaderName ),
                                         dataIn._sourceCodeGLSL.c_str(),
                                         dataIn._sourceCodeGLSL.length(),
                                         FileType::TEXT );
                    }

                    if ( err != FileError::NONE )
                    {
                        Console::errorfn( Locale::Get( _ID( "ERROR_SHADER_SAVE_TEXT_FAILED" ) ), dataIn._shaderName.c_str() );
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
                                     SpvTargetName( dataIn._shaderName ),
                                     (bufferPtr)dataIn._sourceCodeSpirV.data(),
                                     dataIn._sourceCodeSpirV.size() * sizeof( U32 ),
                                     FileType::BINARY );

                    if ( err != FileError::NONE )
                    {
                        Console::errorfn( Locale::Get( _ID( "ERROR_SHADER_SAVE_SPIRV_FAILED" ) ), dataIn._shaderName.c_str() );
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
                    Console::errorfn( Locale::Get( _ID( "ERROR_SHADER_SAVE_REFL_FAILED" ) ), dataIn._shaderName.c_str() );
                }
            } break;
            default: return false;
        };

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
                                ResourcePath{ dataInOut._shaderName },
                                dataInOut._sourceCodeGLSL, FileType::TEXT );
                return err == FileError::NONE;
            } break;
            case LoadData::ShaderCacheType::SPIRV:
            {
                vector<Byte> tempData;
                {
                    err = readFile( SpvCacheLocation(),
                                    SpvTargetName( dataInOut._shaderName ),
                                    tempData,
                                    FileType::BINARY );
                }

                if ( err == FileError::NONE )
                {
                    dataInOut._sourceCodeSpirV.resize( tempData.size() / sizeof( U32 ) );
                    memcpy( dataInOut._sourceCodeSpirV.data(), tempData.data(), tempData.size() );
                    return true;
                }

                return false;
            } break;
            case LoadData::ShaderCacheType::REFLECTION:
            {
                return Reflection::LoadReflectionData( ReflCacheLocation(), ReflTargetName( dataInOut._shaderName ), dataInOut._reflectionData, atomIDsOut );
            } break;
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
        glswSetPath( (Paths::g_assetsLocation + Paths::g_shadersLocation + Paths::Shaders::GLSL::g_GLSLShaderLoc).c_str(), ".glsl" );

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
                stageData._sourceFile = data._sourceFile;
                stageData._sourceName = Str256( data._sourceFile.substr( 0, data._sourceFile.find_first_of( "." ) ) );
                stageData._sourceName.append( "." );
                stageData._sourceName.append( Names::shaderTypes[to_U8( type )] );
                if ( !data._variant.empty() )
                {
                    stageData._sourceName.append( "." + data._variant );
                }
                stageData._definesHash = DefinesHash( data._defines );
                stageData._shaderName.append(stageData._sourceName );
                if ( stageData._definesHash != 0u )
                {
                    stageData._shaderName.append( "." + Util::to_string(stageData._definesHash));
                }
                stageData._shaderName.append("." + shaderAtomExtensionName[to_U8(type)]);

                if ( !loadSourceCode( data._defines, overwrite, stageData, previousUniforms, blockOffset ) )
                {
                    Console::errorfn(Locale::Get(_ID("ERROR_SHADER_LOAD_SOURCE_CODE_FAILED")), stageData._shaderName.c_str(), overwrite ? "TRUE" : "FALSE");
                    return false;
                }

                if ( !loadDataPerFile._programName.empty() )
                {
                    loadDataPerFile._programName.append( "-" );
                }
                loadDataPerFile._programName.append( stageData._shaderName );
            }

            initUniformUploader( loadDataPerFile );
            initDrawDescriptorSetLayout( loadDataPerFile );
        }

        return true;
    }

    namespace
    {
        [[nodiscard]] ShaderStageVisibility GetShaderStageVisibility( const ShaderType type ) noexcept
        {
            switch ( type )
            {
                case ShaderType::FRAGMENT: return ShaderStageVisibility::FRAGMENT;
                case ShaderType::VERTEX: return ShaderStageVisibility::VERTEX;
                case ShaderType::GEOMETRY: return ShaderStageVisibility::GEOMETRY;
                case ShaderType::TESSELLATION_CTRL: return ShaderStageVisibility::TESS_CONTROL;
                case ShaderType::TESSELLATION_EVAL: return ShaderStageVisibility::TESS_EVAL;
                case ShaderType::COMPUTE: return ShaderStageVisibility::COMPUTE;
            };

            return ShaderStageVisibility::NONE;
        }
    };

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
                    DIVIDE_ASSERT( binding._type == DescriptorSetBindingType::COUNT || binding._type == DescriptorSetBindingType::COMBINED_IMAGE_SAMPLER);
                    binding._type = DescriptorSetBindingType::COMBINED_IMAGE_SAMPLER;
                }
                else
                {
                    DIVIDE_ASSERT( binding._type == DescriptorSetBindingType::COUNT || binding._type == DescriptorSetBindingType::IMAGE );
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
                    DIVIDE_ASSERT( binding._type == DescriptorSetBindingType::COUNT || binding._type == DescriptorSetBindingType::UNIFORM_BUFFER );
                    binding._type = DescriptorSetBindingType::UNIFORM_BUFFER;
                }
                else
                {
                    DIVIDE_ASSERT( binding._type == DescriptorSetBindingType::COUNT || binding._type == DescriptorSetBindingType::SHADER_STORAGE_BUFFER );
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

    bool ShaderProgram::uploadUniformData( const PushConstants& data, DescriptorSet& set, GFX::MemoryBarrierCommand& memCmdInOut )
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Graphics );

        bool ret = false;
        for ( auto& blockBuffer : _uniformBlockBuffers )
        {
            for ( const GFX::PushConstant& constant : data.data() )
            {
                blockBuffer.uploadPushConstant( constant );
            }
            if ( blockBuffer.commit( set, memCmdInOut ) )
            {
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

        vk::ShaderStageFlagBits type = vk::ShaderStageFlagBits::eVertex;

        switch ( loadDataInOut._type )
        {
            default:
            case ShaderType::VERTEX:            type = vk::ShaderStageFlagBits::eVertex;                 break;
            case ShaderType::TESSELLATION_CTRL: type = vk::ShaderStageFlagBits::eTessellationControl;    break;
            case ShaderType::TESSELLATION_EVAL: type = vk::ShaderStageFlagBits::eTessellationEvaluation; break;
            case ShaderType::GEOMETRY:          type = vk::ShaderStageFlagBits::eGeometry;               break;
            case ShaderType::FRAGMENT:          type = vk::ShaderStageFlagBits::eFragment;               break;
            case ShaderType::COMPUTE:           type = vk::ShaderStageFlagBits::eCompute;                break;
        };

        bool needGLSL = !s_targetVulkan;
        if ( reloadExisting )
        {
            // Hot reloading will always reparse GLSL source files so the best way to achieve that is to delete cache files
            needGLSL = true;
            if ( !DeleteCache( LoadData::ShaderCacheType::COUNT, loadDataInOut._shaderName ) )
            {
                // We should have cached the existing shader, so a failure here is NOT expected
                DIVIDE_UNEXPECTED_CALL();
            }
        }

        // Load SPIRV code from cache (if needed)
        if ( reloadExisting || !LoadFromCache( LoadData::ShaderCacheType::SPIRV, loadDataInOut, atomIDs ) )
        {
            needGLSL = true;
        }

        // We either have SPIRV code or we explicitly require GLSL code (e.g. for OpenGL)
        if ( needGLSL )
        {
            // Try and load GLSL code from cache (if needed)
            if ( reloadExisting || !LoadFromCache( LoadData::ShaderCacheType::GLSL, loadDataInOut, atomIDs ) )
            {
                // That failed, so re-parse the code
                loadAndParseGLSL( defines, reloadExisting, loadDataInOut, previousUniformsInOut, blockIndexInOut, atomIDs );
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
                DIVIDE_ASSERT( !loadDataInOut._sourceCodeGLSL.empty() );
                if ( !SpirvHelper::GLSLtoSPV( loadDataInOut._type, loadDataInOut._sourceCodeGLSL.c_str(), loadDataInOut._sourceCodeSpirV, s_targetVulkan ) )
                {
                    Console::errorfn( Locale::Get( _ID( "ERROR_SHADER_CONVERSION_SPIRV_FAILED" ) ), loadDataInOut._shaderName.c_str() );
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
        DIVIDE_ASSERT( !loadDataInOut._sourceCodeSpirV.empty() );
        // Time to see if we have any cached reflection data, and, if not, build it
        if ( reloadExisting || !LoadFromCache( LoadData::ShaderCacheType::REFLECTION, loadDataInOut, atomIDs ) )
        {
            // Well, we failed. Time to build our reflection data again
            if ( !SpirvHelper::BuildReflectionData( loadDataInOut._type, loadDataInOut._sourceCodeSpirV, s_targetVulkan, loadDataInOut._reflectionData ) )
            {
                Console::errorfn( Locale::Get( _ID( "ERROR_SHADER_REFLECTION_SPIRV_FAILED" ) ), loadDataInOut._shaderName.c_str() );
                return false;
            }
            // Save reflection data to cache for future use
            SaveToCache( LoadData::ShaderCacheType::REFLECTION, loadDataInOut, atomIDs );
        }
        else
        {
            blockIndexInOut = loadDataInOut._reflectionData._uniformBlockBindingIndex - Reflection::UNIFORM_BLOCK_BINDING_OFFSET;
        }

        if ( !loadDataInOut._sourceCodeGLSL.empty() || !loadDataInOut._sourceCodeSpirV.empty() )
        {
            _usedAtomIDs.insert( begin( atomIDs ), end( atomIDs ) );
            return true;
        }

        return false;
    }

    void ShaderProgram::loadAndParseGLSL( const ModuleDefines& defines,
                                          bool reloadExisting,
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
            glslCodeOut = PreprocessIncludes( ResourcePath( resourceName() ), glslCodeOut, 0, atomIDsInOut, true );
            glslCodeOut = Preprocessor::PreProcess( glslCodeOut, loadDataInOut._shaderName.c_str() );
            glslCodeOut = Reflection::GatherUniformDeclarations( glslCodeOut, loadDataInOut._uniforms );
        }

        if ( !loadDataInOut._uniforms.empty() )
        {
            if ( !previousUniformsInOut.empty() && previousUniformsInOut != loadDataInOut._uniforms )
            {
                ++blockIndexInOut;
            }

            loadDataInOut._reflectionData._uniformBlockBindingSet = to_base( DescriptorSetUsage::PER_DRAW );
            loadDataInOut._reflectionData._uniformBlockBindingIndex = Reflection::UNIFORM_BLOCK_BINDING_OFFSET + blockIndexInOut;

            string& uniformBlock = loadDataInOut._uniformBlock;
            uniformBlock = "layout( ";
            if ( _context.renderAPI() == RenderAPI::Vulkan )
            {
                uniformBlock.append( Util::StringFormat( "set = %d, ", to_base( DescriptorSetUsage::PER_DRAW ) ) );
            }
            uniformBlock.append( "binding = %d, std140 ) uniform %s {" );

            for ( const Reflection::UniformDeclaration& uniform : loadDataInOut._uniforms )
            {
                uniformBlock.append( Util::StringFormat( "\n    %s %s;", uniform._type.c_str(), uniform._name.c_str() ) );
            }
            uniformBlock.append( Util::StringFormat( "\n} %s;", UNIFORM_BLOCK_NAME ) );

            for ( const Reflection::UniformDeclaration& uniform : loadDataInOut._uniforms )
            {
                const auto rawName = uniform._name.substr( 0, uniform._name.find_first_of( "[" ) ).to_string();
                uniformBlock.append( Util::StringFormat( "\n#define %s %s.%s", rawName.c_str(), UNIFORM_BLOCK_NAME, rawName.c_str() ) );
            }

            const U8 layoutIndex = _context.renderAPI() == RenderAPI::Vulkan
                ? loadDataInOut._reflectionData._uniformBlockBindingIndex
                : ShaderProgram::GetGLBindingForDescriptorSlot( DescriptorSetUsage::PER_DRAW,
                                                                loadDataInOut._reflectionData._uniformBlockBindingIndex );

            uniformBlock = Util::StringFormat( uniformBlock, layoutIndex, Util::StringFormat( "dvd_UniformBlock_%lld", blockIndexInOut ).c_str() );

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

        Util::ReplaceStringInPlace( loadDataInOut._sourceCodeGLSL, "//_PROGRAM_NAME_\\", "/*[ " + loadDataInOut._shaderName + " ]*/");
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
        eastl::fixed_vector<U64, 128, true> queuedDeletion;

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
        DIVIDE_ASSERT( evt != FileUpdateEvent::COUNT );

        // Do nothing if the specified file is "deleted". We do not want to break running programs
        // ADD and MODIFY events should get processed as usual
        if ( evt == FileUpdateEvent::DELETE )
        {
            return;
        }

        const U64 atomNameHash = _ID( string{ atomName }.c_str() );
        EraseAtomLocked(atomNameHash);

        //Get list of shader programs that use the atom and rebuild all shaders in list;
        SharedLock<SharedMutex> lock( s_programLock );
        for ( const ShaderProgramMapEntry& entry : s_shaderPrograms )
        {
            if ( entry._program != nullptr )
            {
                for ( const U64 atomID : entry._program->_usedAtomIDs )
                {
                    if ( atomID == atomNameHash )
                    {
                        s_recompileQueue.push( ShaderQueueEntry{ ._program = entry._program } );
                        break;
                    }
                }
            }
        }
    }

};
