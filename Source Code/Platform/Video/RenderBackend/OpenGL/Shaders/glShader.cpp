#include "stdafx.h"

#include "Headers/glShader.h"
#include "Headers/glBufferedPushConstantUploader.h"

#include "Platform/File/Headers/FileManagement.h"
#include "Platform/Video/Headers/GFXDevice.h"
#include "Platform/Video/RenderBackend/OpenGL/Headers/glResources.h"
#include "Platform/Video/RenderBackend/OpenGL/Headers/GLWrapper.h"

#include "Core/Headers/StringHelper.h"
#include "Core/Time/Headers/ProfileTimer.h"

#include "Utility/Headers/Localization.h"

namespace Divide {

namespace {
    constexpr bool g_consumeSPIRVInput = true;
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

    FORCE_INLINE ShaderType GetShaderType(const UseProgramStageMask mask) noexcept {
        if (BitCompare(to_U32(mask), UseProgramStageMask::GL_VERTEX_SHADER_BIT)) {
            return ShaderType::VERTEX;
        }
        if (BitCompare(to_U32(mask), UseProgramStageMask::GL_TESS_CONTROL_SHADER_BIT)) {
            return ShaderType::TESSELLATION_CTRL;
        }
        if (BitCompare(to_U32(mask), UseProgramStageMask::GL_TESS_EVALUATION_SHADER_BIT)) {
            return ShaderType::TESSELLATION_EVAL;
        }
        if (BitCompare(to_U32(mask), UseProgramStageMask::GL_GEOMETRY_SHADER_BIT)) {
            return ShaderType::GEOMETRY;
        }
        if (BitCompare(to_U32(mask), UseProgramStageMask::GL_FRAGMENT_SHADER_BIT)) {
            return ShaderType::FRAGMENT;
        }
        if (BitCompare(to_U32(mask), UseProgramStageMask::GL_COMPUTE_SHADER_BIT)) {
            return ShaderType::COMPUTE;
        }
        
        // Multiple stages!
        return ShaderType::COUNT;
    }

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

}

void glShader::DestroyStaticData() {
    ScopedLock<SharedMutex> w_lock(s_shaderNameLock);
    s_shaderNameMap.clear();
}

glShader::glShader(GFXDevice& context, const Str256& name)
    : GUIDWrapper(),
      GraphicsResource(context, Type::SHADER, getGUID(), _ID(name.c_str())),
      glObject(glObjectType::TYPE_SHADER, context),
      _name(name),
      _stageMask(UseProgramStageMask::GL_NONE_BIT)
{
    std::atomic_init(&_refCount, 0);
}

glShader::~glShader() {
    if (_programHandle != GLUtil::k_invalidObjectID) {
        Console::d_printfn(Locale::Get(_ID("SHADER_DELETE")), name().c_str());
        GL_API::DeleteShaderPrograms(1, &_programHandle);
    }
}

ShaderResult glShader::uploadToGPU(const GLuint parentProgramHandle) {
    if (!_valid) {
        const auto getTimerAndReset = [](Time::ProfileTimer& timer) {
            timer.stop();
            const U64 ret = timer.get();
            timer.reset();
            return ret;
        };

        Time::ProfileTimer shaderCompileTimer{};
        Time::ProfileTimer shaderCompileTimerGPU{};
        Time::ProfileTimer shaderCompileDriveSide{};
        Time::ProfileTimer shaderLinkTimer{};

        if_constexpr(Config::ENABLE_GPU_VALIDATION) {
            shaderCompileTimer.start();
        }
        Console::d_printfn(Locale::Get(_ID("GLSL_LOAD_PROGRAM")), _name.c_str(), getGUID());

        U64 totalCompileTime = 0u;
        U64 programCompileTimeGPU = 0u;
        U64 programCompileTimeGPUMisc = 0u;
        U64 programLogRetrieval = 0u;
        U64 driverLinkTime = 0u;
        U64 uniformCaching = 0u;
        std::array<U64, to_base(ShaderType::COUNT)> programCompileTimeDriver{};
        std::array<U64, to_base(ShaderType::COUNT)> stageCompileTimeGPU{};

        if (!loadFromBinary()) {
            if (_programHandle == GLUtil::k_invalidObjectID) {
                if_constexpr(Config::ENABLE_GPU_VALIDATION) {
                    shaderCompileTimerGPU.start();
                }
                _programHandle = glCreateProgram();
                glProgramParameteri(_programHandle, GL_PROGRAM_BINARY_RETRIEVABLE_HINT, GL_TRUE);
                glProgramParameteri(_programHandle, GL_PROGRAM_SEPARABLE, GL_TRUE);
                if_constexpr(Config::ENABLE_GPU_VALIDATION) {
                    programCompileTimeGPU += getTimerAndReset(shaderCompileTimerGPU);
                }
            }
            if (_programHandle == 0 || _programHandle == GLUtil::k_invalidObjectID) {
                Console::errorfn(Locale::Get(_ID("ERROR_GLSL_CREATE_PROGRAM")), _name.c_str());
                _valid = false;
                return ShaderResult::Failed;
            }

            bool shouldLink = false;

            bool usingSPIRv = false;
            for (const ShaderProgram::LoadData& data : _loadData._data) {
                vector<const char*> sourceCodeCstr{ data._sourceCodeGLSL.c_str() };

                if (!sourceCodeCstr.empty()) {
                    if_constexpr(Config::ENABLE_GPU_VALIDATION) {
                        shaderCompileTimerGPU.start();
                    }
                    const GLuint shader = glCreateShader(GLUtil::glShaderStageTable[to_base(data._type)]);
                    if (shader != 0u) {
                        if_constexpr(Config::ENABLE_GPU_VALIDATION) {
                            shaderCompileDriveSide.start();
                        }
                        if (!g_consumeSPIRVInput || data._sourceCodeSpirV.empty()) {
                            DIVIDE_ASSERT(!usingSPIRv, "glShader::uploadToGPU ERROR. Either all shader stages use SPIRV or non of them do!");

                            glShaderSource(shader, static_cast<GLsizei>(sourceCodeCstr.size()), sourceCodeCstr.data(), nullptr);
                            glCompileShader(shader);
                        } else {
                            usingSPIRv = true;
                            glShaderBinary(1, &shader, GL_SHADER_BINARY_FORMAT_SPIR_V_ARB, data._sourceCodeSpirV.data(), (GLsizei)(data._sourceCodeSpirV.size() * sizeof(U32)));
                            glSpecializeShader(shader, "main", 0, nullptr, nullptr);
                        }
                        if_constexpr(Config::ENABLE_GPU_VALIDATION) {
                            programCompileTimeDriver[to_base(data._type)] = getTimerAndReset(shaderCompileDriveSide);
                        }
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
                            glAttachShader(_programHandle, shader);
                            shouldLink = true;
                        }
                    }
                    if_constexpr(Config::ENABLE_GPU_VALIDATION) {
                        stageCompileTimeGPU[to_base(data._type)] = getTimerAndReset(shaderCompileTimerGPU);
                        programCompileTimeGPU += stageCompileTimeGPU[to_base(data._type)];
                    }
                }
            }

            if (shouldLink) {
                if_constexpr(Config::ENABLE_GPU_VALIDATION) {
                    shaderLinkTimer.start();
                }

                glLinkProgram(_programHandle);

                if_constexpr(Config::ENABLE_GPU_VALIDATION) {
                    driverLinkTime = getTimerAndReset(shaderLinkTimer);
                    programCompileTimeGPU += driverLinkTime;
                }
            }
        }

        if_constexpr(Config::ENABLE_GPU_VALIDATION) {
            shaderCompileTimerGPU.start();
        }
        // And check the result
        GLboolean linkStatus = GL_FALSE;
        glGetProgramiv(_programHandle, GL_LINK_STATUS, &linkStatus);

        // If linking failed, show an error, else print the result in debug builds.
        if (linkStatus == GL_FALSE) {
            GLint logSize = 0;
            glGetProgramiv(_programHandle, GL_INFO_LOG_LENGTH, &logSize);
            string validationBuffer;
            validationBuffer.resize(logSize);

            glGetProgramInfoLog(_programHandle, logSize, nullptr, &validationBuffer[0]);
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
                Console::printfn(Locale::Get(_ID("GLSL_LINK_PROGRAM_LOG_OK")), _name.c_str(), "[OK]", getGUID(), _programHandle);
                glObjectLabel(GL_PROGRAM, _programHandle, -1, _name.c_str());
            }
            _valid = true;
        }
        if_constexpr(Config::ENABLE_GPU_VALIDATION) {
            programLogRetrieval = getTimerAndReset(shaderCompileTimerGPU);
            programCompileTimeGPU += programLogRetrieval;
            programCompileTimeGPUMisc += programLogRetrieval;
        }

        if (_valid) {
            if_constexpr(Config::ENABLE_GPU_VALIDATION) {
                shaderCompileTimerGPU.start();
            }

            if (!_loadData._reflectionData._blockMembers.empty()) {
                DIVIDE_ASSERT(_loadData._reflectionData._blockSize > 0);

                glBufferedPushConstantUploaderDescriptor bufferDescriptor = {};
                bufferDescriptor._programHandle = _programHandle;
                bufferDescriptor._uniformBufferName = _loadData._uniformBlockName.c_str();
                bufferDescriptor._parentShaderName = _name.c_str();
                bufferDescriptor._bindingIndex = _loadData._uniformBlockOffset;
                bufferDescriptor._reflectionData = _loadData._reflectionData;
                _constantUploader = eastl::make_unique<glBufferedPushConstantUploader>(bufferDescriptor);
                _constantUploader->cacheUniforms();
            } else {
                _constantUploader.reset();
            }

            if_constexpr(Config::ENABLE_GPU_VALIDATION) {
                uniformCaching = getTimerAndReset(shaderCompileTimerGPU);
                programCompileTimeGPU += uniformCaching;
                programCompileTimeGPUMisc += uniformCaching;
            }
        }

        if_constexpr(Config::ENABLE_GPU_VALIDATION) {
            totalCompileTime = getTimerAndReset(shaderCompileTimer);

            U8 maxStageGPU = 0u; U64 maxTimeGPU = 0u;
            U8 maxStageDriver = 0u; U64 maxTimeDriver = 0u;
            U64 totalTimeDriver = 0u;

            string perStageTiming = "";
            for (const auto& data : _loadData._data) {
                if (data._type == ShaderType::COUNT) {
                    continue;
                }

                const U64 gpu = stageCompileTimeGPU[to_base(data._type)];
                const U64 driver = programCompileTimeDriver[to_base(data._type)];

                if (gpu > maxTimeGPU) {
                    maxStageGPU = to_base(data._type);
                    maxTimeGPU = gpu;
                }
                if (driver > maxTimeDriver) {
                    maxStageDriver = to_base(data._type);
                    maxTimeDriver = driver;
                }
                totalTimeDriver += driver;

                perStageTiming.append(Util::StringFormat("---- [ %s ] - [%5.2f] - [%5.2f]\n", 
                                      Names::shaderTypes[to_base(data._type)],
                                      Time::MicrosecondsToMilliseconds<F32>(gpu),
                                      Time::MicrosecondsToMilliseconds<F32>(driver)));

                ScopedLock<SharedMutex> w_lock(s_hotspotShaderLock);
                if (maxTimeGPU > s_hotspotShaderGPU._hotspotShaderTimer) {
                    s_hotspotShaderGPU._hotspotShaderTimer = maxTimeGPU;
                    s_hotspotShaderGPU._hotspotShader = data._name.c_str();
                }
                if (maxTimeDriver > s_hotspotShaderDriver._hotspotShaderTimer) {
                    s_hotspotShaderDriver._hotspotShaderTimer = maxTimeDriver;
                    s_hotspotShaderDriver._hotspotShader = data._name.c_str();
                }
                
                const U64 maxPerStage = std::max(maxTimeGPU, maxTimeDriver);

                const bool isLogHotspot = programLogRetrieval > std::max(maxPerStage, driverLinkTime);
                const char* bottleneckGlobal = isLogHotspot ? "Log Retrieval / First use" : (maxPerStage > driverLinkTime ? "Compilation" : "Linking");
                const char* bottleneckPerStage = maxTimeGPU > maxTimeDriver ? "GPU" : "Driver";

                Console::printfn(Locale::Get(_ID("SHADER_COMPILE_TIME_NON_SEPARABLE")),
                                 name().c_str(),                                                                                                    //Shader name
                                 parentProgramHandle,                                                                                               //Pipeline handle
                                 bottleneckGlobal,                                                                                                  //Global hotspot
                                 bottleneckPerStage,                                                                                                //Stage hotspot
                                 Time::MicrosecondsToMilliseconds<F32>(totalCompileTime),                                                           //Total time
                                 Time::MicrosecondsToMilliseconds<F32>(programCompileTimeGPU),                                                      //Total driver time
                                 Time::MicrosecondsToMilliseconds<F32>(programCompileTimeGPU - programCompileTimeGPUMisc),                          //Total driver GPU
                                 Time::MicrosecondsToMilliseconds<F32>(programCompileTimeGPUMisc),                                                  //Total driver MISC
                                 Time::MicrosecondsToMilliseconds<F32>(totalTimeDriver),                                                            //Driver compile time
                                 Time::MicrosecondsToMilliseconds<F32>(maxTimeDriver),                                                              //Max compile time per stage
                                 Time::MicrosecondsToMilliseconds<F32>(driverLinkTime),                                                             //Driver link time
                                 Time::MicrosecondsToMilliseconds<F32>(uniformCaching),
                                 Time::MicrosecondsToMilliseconds<F32>(programLogRetrieval),
                                 perStageTiming.c_str(),
                                 s_hotspotShaderGPU._hotspotShader.c_str(), Time::MicrosecondsToMilliseconds<F32>(s_hotspotShaderGPU._hotspotShaderTimer),
                                 s_hotspotShaderDriver._hotspotShader.c_str(), Time::MicrosecondsToMilliseconds<F32>(s_hotspotShaderDriver._hotspotShaderTimer));
            }
        }
    }

    return _valid ? ShaderResult::OK : ShaderResult::Failed;
}

bool glShader::load(ShaderProgram::ShaderLoadData& data) {
    _valid = false;
    _stageMask = UseProgramStageMask::GL_NONE_BIT;
    _loadData = data;

    bool hasData = false;
    _loadData._reflectionData._targetBlockName = _loadData._uniformBlockName;
    for (ShaderProgram::LoadData& it : _loadData._data) {
        if (it._type == ShaderType::COUNT) {
            continue;
        }
        _stageMask |= GetStageMask(it._type);
        if (it._codeSource == ShaderProgram::LoadData::SourceCodeSource::SOURCE_FILES) {
            Util::ReplaceStringInPlace(it._sourceCodeGLSL, "//_CUSTOM_UNIFORMS_\\", _loadData._uniformBlock);
        }

        ShaderProgram::ParseGLSLSource(_loadData._reflectionData, it, false);

        if (!it._sourceCodeGLSL.empty() || !it._sourceCodeSpirV.empty()) {
            hasData = true;
        }
    }

    if (!hasData) {
        Console::errorfn(Locale::Get(_ID("ERROR_GLSL_NOT_FOUND")), name().c_str());
        return false;
    }

    return true;
}

// ============================ static data =========================== //
/// Remove a shader entity. The shader is deleted only if it isn't referenced by a program
void glShader::RemoveShader(glShader* s) {
    assert(s != nullptr);

    // Try to find it
    const U64 nameHash = s->nameHash();
    ScopedLock<SharedMutex> w_lock(s_shaderNameLock);
    const ShaderMap::iterator it = s_shaderNameMap.find(nameHash);
    if (it != std::end(s_shaderNameMap)) {
        // Subtract one reference from it.
        if (s->SubRef()) {
            // If the new reference count is 0, delete the shader (as in leave it in the object arena)
            s_shaderNameMap.erase(nameHash);
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
                               ShaderProgram::ShaderLoadData& data) {
    // See if we have the shader already loaded
    glShader* shader = GetShader(name);

    bool newShader = false;
    // If we do, and don't need a recompile, just return it
    if (shader == nullptr) {
        // If we can't find it, we create a new one
        ScopedLock<Mutex> w_lock(context.objectArenaMutex());
        shader = new (context.objectArena()) glShader(context, name);
        context.objectArena().DTOR(shader);
        newShader = true;
    }

    return LoadShader(shader, newShader, data);
}


glShader* glShader::LoadShader(glShader * shader,
                               const bool isNew,
                               ShaderProgram::ShaderLoadData& data) {

    // At this stage, we have a valid Shader object, so load the source code
    if (shader->load(data)) {
        if (isNew) {
            // If we loaded the source code successfully,  register it
            ScopedLock<SharedMutex> w_lock(s_shaderNameLock);
            s_shaderNameMap.insert({ shader->nameHash(), shader });
        }
    }//else ignore. it's somewhere in the object arena

    return shader;
}

bool glShader::loadFromBinary() {
    _loadedFromBinary = false;

    // Load the program from the binary file, if available and allowed, to avoid linking.
    if (ShaderProgram::UseShaderBinaryCache()) {

        const Str256 decoratedName{ ShaderProgram::DecorateFileName(_name) };
        const ResourcePath binaryPath{ decoratedName + g_binaryBinExtension };
        const ResourcePath formatPath{ decoratedName + g_binaryFmtExtension };
        const ResourcePath cachePath{ Paths::g_cacheLocation + Paths::Shaders::g_cacheLocationBin };

        // Load the program's binary format from file
        vector<Byte> data;
        FileError err = readFile(cachePath, formatPath, data, FileType::BINARY);
        if (err == FileError::NONE && !data.empty()) {

            const GLenum binaryFormat = *reinterpret_cast<GLenum*>(data.data());
            if (binaryFormat != GL_NONE) {
                return false;
            }

            err = readFile(cachePath, binaryPath, data, FileType::BINARY);
            if (err == FileError::NONE && !data.empty()) {

                // Load binary code on the GPU
                _programHandle = glCreateProgram();
                glProgramBinary(_programHandle, binaryFormat, (bufferPtr)data.data(), static_cast<GLint>(data.size()));
                // Check if the program linked successfully on load
                GLboolean success = GL_FALSE;
                glGetProgramiv(_programHandle, GL_LINK_STATUS, &success);
                // If it loaded properly set all appropriate flags (this also prevents low level access to the program's shaders)
                _loadedFromBinary = _valid = success == GL_TRUE;
            }
        }
    }

    return _loadedFromBinary;
}

bool glShader::DumpBinary(const GLuint handle, const Str256& name) {
    static eastl::set<GLuint> s_dumpedBinaries;
    static Mutex s_dumpedBinariesLock;
    {
        UniqueLock<Mutex> w_lock(s_dumpedBinariesLock);
        if (!s_dumpedBinaries.insert(handle).second) {
            //Already dumped!
            return true;
        }
    }

    bool ret = false;
    // Get the size of the binary code
    GLsizei binaryLength = 0;
    glGetProgramiv(handle, GL_PROGRAM_BINARY_LENGTH, &binaryLength);
    // allocate a big enough buffer to hold it
    char* binary = MemoryManager_NEW char[binaryLength];
    DIVIDE_ASSERT(binary != nullptr, "glShader error: could not allocate memory for the program binary!");
    SCOPE_EXIT{
        // delete our local code buffer
        MemoryManager::DELETE(binary);
    };

    // and fill the buffer with the binary code
    GLenum binaryFormat = GL_NONE;
    glGetProgramBinary(handle, binaryLength, nullptr, &binaryFormat, binary);
    if (binaryFormat != GL_NONE) {
        const Str256 decoratedName{ glShaderProgram::DecorateFileName(name) };
        const ResourcePath binaryPath { decoratedName + g_binaryBinExtension };
        const ResourcePath formatPath { decoratedName + g_binaryFmtExtension };
        const ResourcePath cachePath  { Paths::g_cacheLocation + Paths::Shaders::g_cacheLocationBin };

        // dump the buffer to file
        FileError err = writeFile(cachePath, binaryPath, binary, to_size(binaryLength), FileType::BINARY);
        if (err == FileError::NONE) {
            // dump the format to a separate file (highly non-optimised. Should dump formats to a database instead)
            err = writeFile(cachePath, formatPath, &binaryFormat, sizeof(GLenum), FileType::BINARY);
            ret = err == FileError::NONE;
        }
    }

    return ret;
}

void glShader::uploadPushConstants(const PushConstants& constants) const {
    if (_constantUploader != nullptr) {
        for (const GFX::PushConstant& constant : constants.data()) {
            _constantUploader->uploadPushConstant(constant);
        }
        _constantUploader->commit();
    }
}

void glShader::prepare() const {
    if (_constantUploader != nullptr) {
        _constantUploader->prepare();
    }
}

void glShader::onParentValidation() {
    for (auto& shader : _shaderIDs) {
        if (shader != GLUtil::k_invalidObjectID) {
            glDetachShader(_programHandle, shader);
            glDeleteShader(shader);
        }
    }

    _shaderIDs.resize(0);
}

} // namespace Divide
