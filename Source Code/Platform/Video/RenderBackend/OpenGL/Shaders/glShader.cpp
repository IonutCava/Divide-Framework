#include "stdafx.h"

#include "Headers/glShader.h"
#include "Headers/glShaderProgram.h"

#include "Platform/File/Headers/FileManagement.h"
#include "Platform/Video/Headers/GFXDevice.h"
#include "Platform/Video/RenderBackend/OpenGL/Headers/glResources.h"
#include "Platform/Video/RenderBackend/OpenGL/Headers/GLWrapper.h"

#include "Core/Headers/StringHelper.h"
#include "Core/Time/Headers/ProfileTimer.h"

#include "Utility/Headers/Localization.h"

namespace Divide {

namespace {
    constexpr const char* g_binaryBinExtension = ".bin";
    constexpr const char* g_binaryFmtExtension = ".fmt";

    struct MetricsShaderTimer {
        eastl::string _hotspotShader = "";
        U64 _hotspotShaderTimer = 0u;
    };

    SharedMutex s_hotspotShaderLock;
    static MetricsShaderTimer s_hotspotShaderGPU;
    static MetricsShaderTimer s_hotspotShaderDriver;

    size_t g_validationBufferMaxSize = 4096 * 16;

    FORCE_INLINE UseProgramStageMask GetStageMask(const ShaderType type) noexcept {
        switch (type) {
            case ShaderType::VERTEX: return UseProgramStageMask::GL_VERTEX_SHADER_BIT;
            case ShaderType::TESSELLATION_CTRL: return UseProgramStageMask::GL_TESS_CONTROL_SHADER_BIT;
            case ShaderType::TESSELLATION_EVAL: return UseProgramStageMask::GL_TESS_EVALUATION_SHADER_BIT;
            case ShaderType::GEOMETRY: return UseProgramStageMask::GL_GEOMETRY_SHADER_BIT;
            case ShaderType::FRAGMENT: return UseProgramStageMask::GL_FRAGMENT_SHADER_BIT;
            case ShaderType::COMPUTE: return UseProgramStageMask::GL_COMPUTE_SHADER_BIT;
            default: break;
        }

        return UseProgramStageMask::GL_NONE_BIT;
    }
}

SharedMutex glShader::s_shaderNameLock;
glShader::ShaderMap glShader::s_shaderNameMap;

void glShader::InitStaticData() {
    NOP();
}

void glShader::DestroyStaticData() {
    ScopedLock<SharedMutex> w_lock(s_shaderNameLock);
    s_shaderNameMap.clear();
}

glShader::glShader(GFXDevice& context, const Str256& name)
    : GUIDWrapper(),
      GraphicsResource(context, Type::SHADER, getGUID(), _ID(name.c_str())),
      glObject(glObjectType::TYPE_SHADER, context),
      _name(name)
{
    std::atomic_init(&_refCount, 0u);
}

glShader::~glShader() {
    if (_handle != GLUtil::k_invalidObjectID) {
        Console::d_printfn(Locale::Get(_ID("SHADER_DELETE")), name().c_str());
        GL_API::DeleteShaderPrograms(1, &_handle);
    }
}

namespace {
    struct TimingData {
        U64 _totalTime = 0u;
        U64 _linkTime = 0u;
        U64 _linkLogRetrievalTime = 0u;
        std::array<U64, to_base(ShaderType::COUNT)> _stageCompileTime{};
        std::array<U64, to_base(ShaderType::COUNT)> _stageCompileLogRetrievalTime{};
    };
};

ShaderResult glShader::uploadToGPU(const GLuint parentProgramHandle) {
    if (!_valid) {
        const auto getTimerAndReset = [](Time::ProfileTimer& timer) {
            timer.stop();
            const U64 ret = timer.get();
            timer.reset();
            return ret;
        };

        TimingData timingData{};

        Time::ProfileTimer timers[2];

        Console::d_printfn(Locale::Get(_ID("GLSL_LOAD_PROGRAM")), _name.c_str(), getGUID());

        std::array<U64, to_base(ShaderType::COUNT)> stageCompileTimeGPU{};

        if_constexpr(Config::ENABLE_GPU_VALIDATION) {
            timers[0].start();
        }

        if (_handle != GLUtil::k_invalidObjectID) {
            GL_API::DeleteShaderPrograms(1, &_handle);
        }

        _handle = glCreateProgram();
        glProgramParameteri(_handle, GL_PROGRAM_BINARY_RETRIEVABLE_HINT, GL_FALSE);
        glProgramParameteri(_handle, GL_PROGRAM_SEPARABLE, GL_TRUE);

        if (_handle == 0u || _handle == GLUtil::k_invalidObjectID) {
            Console::errorfn(Locale::Get(_ID("ERROR_GLSL_CREATE_PROGRAM")), _name.c_str());
            _valid = false;
            return ShaderResult::Failed;
        }

        bool shouldLink = false;
        for (ShaderProgram::LoadData& data : _loadData) {
            assert(!data._compiled);

            if_constexpr(Config::ENABLE_GPU_VALIDATION) {
                timers[1].start();
            }
            GLuint shader = GLUtil::k_invalidObjectID;
            DIVIDE_ASSERT(shader != 0u);
            if (!data._sourceCodeSpirV.empty()) {
                shader = glCreateShader(GLUtil::glShaderStageTable[to_base(data._type)]);
                glShaderBinary(1, &shader, GL_SHADER_BINARY_FORMAT_SPIR_V_ARB, data._sourceCodeSpirV.data(), (GLsizei)(data._sourceCodeSpirV.size() * sizeof(U32)));
                glSpecializeShader(shader, "main", 0, nullptr, nullptr);
            } else if (!data._sourceCodeGLSL.empty()) {
                shader = glCreateShader(GLUtil::glShaderStageTable[to_base(data._type)]);
                vector<const char*> sourceCodeCstr{ data._sourceCodeGLSL.c_str() };
                glShaderSource(shader, static_cast<GLsizei>(sourceCodeCstr.size()), sourceCodeCstr.data(), nullptr);
                glCompileShader(shader);
            }

            if_constexpr(Config::ENABLE_GPU_VALIDATION) {
                timingData._stageCompileTime[to_base(data._type)] += getTimerAndReset(timers[1]);
            }
            if_constexpr(Config::ENABLE_GPU_VALIDATION) {
                timers[1].start();
            }
            if (shader != GLUtil::k_invalidObjectID) {
                GLboolean compiled = 0;
                glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
                if (compiled == GL_FALSE) {
                    // error
                    GLint logSize = 0;
                    glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &logSize);
                    string validationBuffer;
                    validationBuffer.resize(logSize);

                    glGetShaderInfoLog(shader, logSize, &logSize, &validationBuffer[0]);
                    if (validationBuffer.size() > g_validationBufferMaxSize) {
                        // On some systems, the program's disassembly is printed, and that can get quite large
                        validationBuffer.resize(std::strlen(Locale::Get(_ID("ERROR_GLSL_COMPILE"))) * 2 + g_validationBufferMaxSize);
                        // Use the simple "truncate and inform user" system (a.k.a. add dots and delete the rest)
                        validationBuffer.append(" ... ");
                    }

                    Console::errorfn(Locale::Get(_ID("ERROR_GLSL_COMPILE")), _name.c_str(), shader, Names::shaderTypes[to_base(data._type)], validationBuffer.c_str());

                    glDeleteShader(shader);
                } else {
                    _shaderIDs.push_back(shader);
                    glAttachShader(_handle, shader);
                    shouldLink = true;
                    data._compiled = true;
                }
            }
            if_constexpr(Config::ENABLE_GPU_VALIDATION) {
                timingData._stageCompileLogRetrievalTime[to_base(data._type)] += getTimerAndReset(timers[1]);
            }
        }

        if_constexpr(Config::ENABLE_GPU_VALIDATION) {
            timers[1].start();
        }
        if (shouldLink) {
            assert(!_linked);
            glLinkProgram(_handle);
            _linked = true;
        }

        if_constexpr(Config::ENABLE_GPU_VALIDATION) {
            timingData._linkTime = getTimerAndReset(timers[1]);
            timers[1].start();
        }
        // And check the result
        GLboolean linkStatus = GL_FALSE;
        glGetProgramiv(_handle, GL_LINK_STATUS, &linkStatus);

        // If linking failed, show an error, else print the result in debug builds.
        if (linkStatus == GL_FALSE) {
            GLint logSize = 0;
            glGetProgramiv(_handle, GL_INFO_LOG_LENGTH, &logSize);
            string validationBuffer;
            validationBuffer.resize(logSize);

            glGetProgramInfoLog(_handle, logSize, nullptr, &validationBuffer[0]);
            if (validationBuffer.size() > g_validationBufferMaxSize) {
                // On some systems, the program's disassembly is printed, and that can get quite large
                validationBuffer.resize(std::strlen(Locale::Get(_ID("GLSL_LINK_PROGRAM_LOG"))) + g_validationBufferMaxSize);
                // Use the simple "truncate and inform user" system (a.k.a. add dots and delete the rest)
                validationBuffer.append(" ... ");
            }

            Console::errorfn(Locale::Get(_ID("GLSL_LINK_PROGRAM_LOG")), _name.c_str(), validationBuffer.c_str(), getGUID());
            glShaderProgram::Idle(_context.context());
        } else {
            if_constexpr(Config::ENABLE_GPU_VALIDATION) {
                Console::printfn(Locale::Get(_ID("GLSL_LINK_PROGRAM_LOG_OK")), _name.c_str(), "[OK]", getGUID(), _handle);
                glObjectLabel(GL_PROGRAM, _handle, -1, _name.c_str());
            }
            _valid = true;
        }

        if_constexpr(Config::ENABLE_GPU_VALIDATION) {
            timingData._linkLogRetrievalTime = getTimerAndReset(timers[1]);
            timingData._totalTime = getTimerAndReset(timers[0]);
        }

        U8 hotspotIndex = 0u;
        U64 maxHotspotTimer = 0u;
        string perStageTiming = "";
        for (U8 i = 0u; i < to_base(ShaderType::COUNT); ++i) {
            perStageTiming.append(Util::StringFormat("---- [ %s ] - [%5.5f  ms] - [%5.5f  ms]\n",
                                                     Names::shaderTypes[i],
                                                     Time::MicrosecondsToMilliseconds<F32>(timingData._stageCompileTime[i]),
                                                     Time::MicrosecondsToMilliseconds<F32>(timingData._stageCompileLogRetrievalTime[i])));
            if (timingData._stageCompileTime[i] > maxHotspotTimer) {
                maxHotspotTimer = timingData._stageCompileTime[i];
                hotspotIndex = i;
            }
            if (timingData._stageCompileLogRetrievalTime[i] > maxHotspotTimer) {
                maxHotspotTimer = timingData._stageCompileLogRetrievalTime[i];
                hotspotIndex = i;
            }
        }

        Console::printfn(Locale::Get(_ID("SHADER_TIMING_INFO")),
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

bool glShader::load(const ShaderProgram::ShaderLoadData& data) {
    _loadData = data;

    _valid = false; _linked = false; 

    if (_handle != GLUtil::k_invalidObjectID) {
        GL_API::DeleteShaderPrograms(1, &_handle);
    }

    _stageMask = UseProgramStageMask::GL_NONE_BIT;
    for (const ShaderProgram::LoadData& it : _loadData) {
        if (it._type == ShaderType::COUNT) {
            continue;
        }
        assert(!it._sourceCodeGLSL.empty() || !it._sourceCodeSpirV.empty());
        _stageMask |= GetStageMask(it._type);
    }

    if (_stageMask == UseProgramStageMask::GL_NONE_BIT) {
        Console::errorfn(Locale::Get(_ID("ERROR_GLSL_NOT_FOUND")), name().c_str());
        return false;
    }

    return true;
}

// ============================ static data =========================== //
/// Remove a shader entity. The shader is deleted only if it isn't referenced by a program
void glShader::RemoveShader(glShader*& s, const bool force) {
    assert(s != nullptr);

    // Try to find it
    const U64 nameHash = s->nameHash();
    ScopedLock<SharedMutex> w_lock(s_shaderNameLock);
    const ShaderMap::iterator it = s_shaderNameMap.find(nameHash);
    if (it != std::end(s_shaderNameMap)) {
        // Subtract one reference from it.
        const size_t newRefCount = s->SubRef();
        if (force || newRefCount == 0) {
            // If the new reference count is 0, delete the shader (as in leave it in the object arena)
            s_shaderNameMap.erase(nameHash);
            MemoryManager::DELETE(s);
        }
    }
}

/// Return a new shader reference
glShader* glShader::GetShader(const Str256& name) {
    // Try to find the shader
    SharedLock<SharedMutex> r_lock(s_shaderNameLock);
    const ShaderMap::iterator it = s_shaderNameMap.find(_ID(name.c_str()));
    if (it != std::end(s_shaderNameMap)) {
        return it->second;
    }

    return nullptr;
}

/// Load a shader by name, source code and stage
glShader* glShader::LoadShader(GFXDevice& context,
                               const Str256& name,
                               const bool overwriteExisting,
                               ShaderProgram::ShaderLoadData& data) {
    // See if we have the shader already loaded
    glShader* shader = GetShader(name);
    if (overwriteExisting && shader != nullptr) {
        RemoveShader(shader, true);
    }

    // If we do, and don't need a recompile, just return it
    if (shader == nullptr) {
        shader = MemoryManager_NEW glShader(context, name);

        // If we loaded the source code successfully,  register it
        ScopedLock<SharedMutex> w_lock(s_shaderNameLock);
        s_shaderNameMap.insert({ shader->nameHash(), shader });

        // At this stage, we have a valid Shader object, so load the source code
        if (!shader->load(data)) {
            DIVIDE_UNEXPECTED_CALL();
        }
    } else {
        shader->AddRef();
        Console::d_printfn(Locale::Get(_ID("SHADER_MANAGER_GET_INC")), shader->name().c_str(), shader->GetRef());
    }

    return shader;
}

void glShader::onParentValidation() {
    for (auto& shader : _shaderIDs) {
        if (shader != GLUtil::k_invalidObjectID) {
            glDetachShader(_handle, shader);
            glDeleteShader(shader);
        }
    }

    _shaderIDs.resize(0);
}

} // namespace Divide
