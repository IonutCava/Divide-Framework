

#include "Headers/glShader.h"
#include "Headers/glShaderProgram.h"

#include "Platform/File/Headers/FileManagement.h"
#include "Platform/Video/Headers/GFXDevice.h"
#include "Platform/Video/RenderBackend/OpenGL/Headers/glResources.h"
#include "Platform/Video/RenderBackend/OpenGL/Headers/GLWrapper.h"

#include "Core/Time/Headers/ProfileTimer.h"

#include "Utility/Headers/Localization.h"

namespace Divide {

namespace
{
    constexpr bool g_useSPIRVBinaryCode = false;

    size_t g_validationBufferMaxSize = 4096 * 16;

    FORCE_INLINE gl46core::UseProgramStageMask GetStageMask(const ShaderType type) noexcept
    {
        switch (type)
        {
            case ShaderType::VERTEX:            return gl46core::UseProgramStageMask::GL_VERTEX_SHADER_BIT;
            case ShaderType::TESSELLATION_CTRL: return gl46core::UseProgramStageMask::GL_TESS_CONTROL_SHADER_BIT;
            case ShaderType::TESSELLATION_EVAL: return gl46core::UseProgramStageMask::GL_TESS_EVALUATION_SHADER_BIT;
            case ShaderType::GEOMETRY:          return gl46core::UseProgramStageMask::GL_GEOMETRY_SHADER_BIT;
            case ShaderType::FRAGMENT:          return gl46core::UseProgramStageMask::GL_FRAGMENT_SHADER_BIT;
            case ShaderType::COMPUTE:           return gl46core::UseProgramStageMask::GL_COMPUTE_SHADER_BIT;
            case ShaderType::MESH_NV:           return gl46core::UseProgramStageMask::GL_MESH_SHADER_BIT_NV;
            case ShaderType::TASK_NV:           return gl46core::UseProgramStageMask::GL_TASK_SHADER_BIT_NV;
            default:
            case ShaderType::COUNT:             break;
        }

        return gl46core::UseProgramStageMask::GL_NONE_BIT;
    }

    struct TimingData
    {
        U64 _totalTime{ 0u };
        U64 _linkTime{ 0u };
        U64 _linkLogRetrievalTime{ 0u };
        std::array<U64, to_base(ShaderType::COUNT)> _stageCompileTime{};
        std::array<U64, to_base(ShaderType::COUNT)> _stageCompileLogRetrievalTime{};
    };
}

glShader::glShader(GFXDevice& context, const std::string_view name, const U32 generation)
    : ShaderModule(context, name, generation)
{
}

glShader::~glShader()
{
    if (_handle != GL_NULL_HANDLE)
    {
        Console::d_printfn(LOCALE_STR("SHADER_DELETE"), name().c_str());
        DIVIDE_EXPECTED_CALL( GL_API::DeleteShaderPrograms(1, &_handle) );
    }
}

ShaderResult glShader::uploadToGPU()
{
    if (!_valid)
    {
        const auto getTimerAndReset = [](Time::ProfileTimer& timer)
        {
            timer.stop();
            const U64 ret = timer.get();
            timer.reset();
            return ret;
        };

        TimingData timingData{};

        Time::ProfileTimer timers[2];

        Console::d_printfn(LOCALE_STR("GLSL_LOAD_PROGRAM"), _name.c_str(), getGUID());

        if constexpr(Config::ENABLE_GPU_VALIDATION)
        {
            timers[0].start();
        }

        if (_handle != GL_NULL_HANDLE)
        {
            DIVIDE_EXPECTED_CALL( GL_API::DeleteShaderPrograms(1, &_handle) );
        }

        _handle = gl46core::glCreateProgram();
        gl46core::glProgramParameteri(_handle, gl46core::GL_PROGRAM_BINARY_RETRIEVABLE_HINT, gl46core::GL_FALSE);
        gl46core::glProgramParameteri(_handle, gl46core::GL_PROGRAM_SEPARABLE,               gl46core::GL_TRUE);

        if (_handle == 0u || _handle == GL_NULL_HANDLE)
        {
            Console::errorfn(LOCALE_STR("ERROR_GLSL_CREATE_PROGRAM"), _name.c_str());
            _valid = false;
            return ShaderResult::Failed;
        }

        bool shouldLink = false;
        for (ShaderProgram::LoadData& data : _loadData)
        {
            if (data._type == ShaderType::COUNT)
            {
                // stage not specified from the current file. Skip.
                continue;
            }

            assert(!data._compiled);

            if constexpr(Config::ENABLE_GPU_VALIDATION)
            {
                timers[1].start();
            }

            gl46core::GLuint shader = GL_NULL_HANDLE;
            DIVIDE_GPU_ASSERT(shader != 0u && !data._sourceCodeSpirV.empty() && !data._sourceCodeGLSL.empty());

            if constexpr(g_useSPIRVBinaryCode)
            {
                shader = gl46core::glCreateShader(GLUtil::glShaderStageTable[to_base(data._type)]);
                gl46core::glShaderBinary(1, &shader, gl46core::GL_SHADER_BINARY_FORMAT_SPIR_V_ARB, data._sourceCodeSpirV.data(), (gl46core::GLsizei)(data._sourceCodeSpirV.size() * sizeof(SpvWord)));
                gl46core::glSpecializeShader(shader, "main", 0, nullptr, nullptr);
            }
            else
            {
                shader = gl46core::glCreateShader(GLUtil::glShaderStageTable[to_base(data._type)]);
                const char* src = data._sourceCodeGLSL.c_str();
                gl46core::glShaderSource(shader, 1u, &src, nullptr);
                gl46core::glCompileShader(shader);
            }

            if constexpr(Config::ENABLE_GPU_VALIDATION)
            {
                timingData._stageCompileTime[to_base(data._type)] += getTimerAndReset(timers[1]);
                timers[1].start();
            }

            if (shader != GL_NULL_HANDLE)
            {
                gl46core::GLboolean compiled = 0;
                gl46core::glGetShaderiv(shader, gl46core::GL_COMPILE_STATUS, &compiled);
                if (compiled == gl46core::GL_FALSE)
                {
                    // error
                    gl46core::GLint logSize = 0;
                    gl46core::glGetShaderiv(shader, gl46core::GL_INFO_LOG_LENGTH, &logSize);
                    string validationBuffer;
                    validationBuffer.resize(to_size(logSize) );

                    gl46core::glGetShaderInfoLog(shader, logSize, &logSize, &validationBuffer[0]);
                    if (validationBuffer.size() > g_validationBufferMaxSize)
                    {
                        // On some systems, the program's disassembly is printed, and that can get quite large
                        validationBuffer.resize(std::strlen(LOCALE_STR("ERROR_GLSL_COMPILE")) * 2 + g_validationBufferMaxSize);
                        // Use the simple "truncate and inform user" system (a.k.a. add dots and delete the rest)
                        validationBuffer.append(" ... ");
                    }

                    Console::errorfn(LOCALE_STR("ERROR_GLSL_COMPILE"), _name.c_str(), shader, Names::shaderTypes[to_base(data._type)], validationBuffer.c_str());

                    gl46core::glDeleteShader(shader);
                }
                else
                {
                    _shaderIDs.push_back(shader);
                    gl46core::glAttachShader(_handle, shader);
                    shouldLink = true;
                    data._compiled = true;
                }
            }

            if constexpr(Config::ENABLE_GPU_VALIDATION)
            {
                timingData._stageCompileLogRetrievalTime[to_base(data._type)] += getTimerAndReset(timers[1]);
            }
        }

        if constexpr(Config::ENABLE_GPU_VALIDATION)
        {
            timers[1].start();
        }

        if (shouldLink)
        {
            assert(!_linked);
            gl46core::glLinkProgram(_handle);
            _linked = true;
        }

        if constexpr(Config::ENABLE_GPU_VALIDATION)
        {
            timingData._linkTime = getTimerAndReset(timers[1]);
            timers[1].start();
        }

        // And check the result
        gl46core::GLboolean linkStatus = gl46core::GL_FALSE;
        gl46core::glGetProgramiv(_handle, gl46core::GL_LINK_STATUS, &linkStatus);

        // If linking failed, show an error, else print the result in debug builds.
        if (linkStatus == gl46core::GL_FALSE)
        {
            gl46core::GLint logSize = 0;
            gl46core::glGetProgramiv(_handle, gl46core::GL_INFO_LOG_LENGTH, &logSize);
            string validationBuffer;
            validationBuffer.resize(to_size(logSize));

            gl46core::glGetProgramInfoLog(_handle, logSize, nullptr, &validationBuffer[0]);
            if (validationBuffer.size() > g_validationBufferMaxSize)
            {
                // On some systems, the program's disassembly is printed, and that can get quite large
                validationBuffer.resize(std::strlen(LOCALE_STR("GLSL_LINK_PROGRAM_LOG")) + g_validationBufferMaxSize);
                // Use the simple "truncate and inform user" system (a.k.a. add dots and delete the rest)
                validationBuffer.append(" ... ");
            }

            Console::errorfn(LOCALE_STR("GLSL_LINK_PROGRAM_LOG"), _name.c_str(), validationBuffer.c_str(), getGUID());
            glShaderProgram::Idle(_context.context());
        }
        else
        {
            if constexpr(Config::ENABLE_GPU_VALIDATION)
            {
                Console::printfn(LOCALE_STR("GLSL_LINK_PROGRAM_LOG_OK"), _name.c_str(), "[OK]", getGUID(), _handle);
                gl46core::glObjectLabel( gl46core::GL_PROGRAM,
                                         _handle,
                                         -1,
                                         _name.c_str() );
            }
            _valid = true;
        }

        if constexpr(Config::ENABLE_GPU_VALIDATION)
        {
            timingData._linkLogRetrievalTime = getTimerAndReset(timers[1]);
            timingData._totalTime = getTimerAndReset(timers[0]);
        }

        U8 hotspotIndex = 0u;
        U64 maxHotspotTimer = 0u;
        string perStageTiming = "";
        for (U8 i = 0u; i < to_base(ShaderType::COUNT); ++i)
        {
            perStageTiming.append(Util::StringFormat("---- [ {} ] - [{:5.5f} ms] - [{:5.5f}  ms]\n",
                                                     Names::shaderTypes[i],
                                                     Time::MicrosecondsToMilliseconds<F32>(timingData._stageCompileTime[i]),
                                                     Time::MicrosecondsToMilliseconds<F32>(timingData._stageCompileLogRetrievalTime[i])));
            if (timingData._stageCompileTime[i] > maxHotspotTimer)
            {
                maxHotspotTimer = timingData._stageCompileTime[i];
                hotspotIndex = i;
            }

            if (timingData._stageCompileLogRetrievalTime[i] > maxHotspotTimer)
            {
                maxHotspotTimer = timingData._stageCompileLogRetrievalTime[i];
                hotspotIndex = i;
            }
        }

        Console::printfn(LOCALE_STR("SHADER_TIMING_INFO"),
                         name().c_str(),
                         _handle,
                         Time::MicrosecondsToMilliseconds<F32>(timingData._totalTime),
                         Time::MicrosecondsToMilliseconds<F32>(timingData._linkTime),
                         Time::MicrosecondsToMilliseconds<F32>(timingData._linkLogRetrievalTime),
                         Names::shaderTypes[hotspotIndex],
                         perStageTiming.c_str());
    }

    return _valid ? ShaderResult::OK : ShaderResult::Failed;
}

bool glShader::load(const ShaderProgram::ShaderLoadData& data)
{
    _loadData = data;

    _valid = false; _linked = false; 

    if (_handle != GL_NULL_HANDLE)
    {
        DIVIDE_EXPECTED_CALL( GL_API::DeleteShaderPrograms(1, &_handle) );
    }

    _stageMask = gl46core::UseProgramStageMask::GL_NONE_BIT;
    for (const ShaderProgram::LoadData& it : _loadData)
    {
        if (it._type == ShaderType::COUNT)
        {
            continue;
        }

        assert(!it._sourceCodeGLSL.empty() || !it._sourceCodeSpirV.empty());
        _stageMask |= GetStageMask(it._type);
    }

    if (_stageMask == gl46core::UseProgramStageMask::GL_NONE_BIT)
    {
        Console::errorfn(LOCALE_STR("ERROR_GLSL_NOT_FOUND"), name().c_str());
        return false;
    }

    return true;
}

/// Load a shader by name, source code and stage
glShaderEntry glShader::LoadShader(GFXDevice& context,
                                   glShaderProgram* program,
                                   const std::string_view name,
                                   const U32 targetGeneration,
                                   ShaderProgram::ShaderLoadData& data)
{
    glShaderEntry ret
    {
        ._fileHash = _ID( name ),
        ._generation = targetGeneration
    };
    {
        // If we loaded the source code successfully,  register it
        LockGuard<SharedMutex> w_lock(ShaderModule::s_shaderNameLock);
        auto& shader_ptr = s_shaderNameMap[ret._fileHash];
        if (shader_ptr == nullptr || shader_ptr->generation() < ret._generation )
        {
            shader_ptr.reset( new glShader( context, name, ret._generation ) );

            // At this stage, we have a valid Shader object, so load the source code
            DIVIDE_EXPECTED_CALL( static_cast<glShader*>(shader_ptr.get())->load(data) );
        } 
        else 
        {
            Console::d_printfn(LOCALE_STR("SHADER_MANAGER_GET_INC"), shader_ptr->name().c_str());
        }

        ret._shader = static_cast<glShader*>(shader_ptr.get());
    }

    ret._shader->registerParent(program);
    return ret;
}

void glShader::onParentValidation()
{
    for (auto& shader : _shaderIDs)
    {
        if (shader != GL_NULL_HANDLE)
        {
            gl46core::glDetachShader(_handle, shader);
            gl46core::glDeleteShader(shader);
        }
    }

    efficient_clear( _shaderIDs );
}

void glShader::uploadPushConstants(const PushConstantsStruct& pushConstants)
{
    if (_pushConstantsLocation == -2)
    {
        _pushConstantsLocation = gl46core::glGetUniformLocation( _handle, "PushConstantData" );
    }

    if ( _pushConstantsLocation > -1 )
    {
        gl46core::glProgramUniformMatrix4fv(_handle, _pushConstantsLocation, 2, gl46core::GL_FALSE, pushConstants.dataPtr());
    }
}
} // namespace Divide
