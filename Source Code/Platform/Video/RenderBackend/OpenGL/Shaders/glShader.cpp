#include "stdafx.h"

#include "Headers/glShader.h"

#include "Platform/File/Headers/FileManagement.h"
#include "Platform/Video/Headers/GFXDevice.h"
#include "Platform/Video/RenderBackend/OpenGL/Headers/glResources.h"
#include "Platform/Video/RenderBackend/OpenGL/Headers/GLWrapper.h"

#include "Core/Headers/StringHelper.h"
#include "Core/Time/Headers/ProfileTimer.h"
#include "Headers/glBufferedPushConstantUploader.h"
#include "Headers/glUniformPushConstantUploader.h"
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

    FORCE_INLINE string GetUniformBufferName(const glShader* const shader) {
        return "dvd_UniformBlock_" + Util::to_string(shader->getGUID());
    }
}

SharedMutex glShader::_shaderNameLock;
glShader::ShaderMap glShader::_shaderNameMap;

glShader::glShader(GFXDevice& context, const Str256& name)
    : GUIDWrapper(),
      GraphicsResource(context, Type::SHADER, getGUID(), _ID(name.c_str())),
      glObject(glObjectType::TYPE_SHADER, context),
      _name(name),
      _stageMask(UseProgramStageMask::GL_NONE_BIT)
{
    std::atomic_init(&_refCount, 0);
    _shaderIDs.fill(GLUtil::k_invalidObjectID);
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

        Time::ProfileTimer shaderCompileTimer;

        Time::ProfileTimer shaderCompileTimerGPU;
        Time::ProfileTimer shaderCompileDriveSide;
        Time::ProfileTimer shaderLinkTimer;

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

        bool isSeparable = false;
        const GLuint blockIndex = to_U32(ShaderBufferLocation::UNIFORM_BLOCK) + _loadData._uniformIndex;
        if (!loadFromBinary()) {

            U8 stageCount = 0u;
            for (const LoadData& it : _loadData._data) {
                if (it._type != ShaderType::COUNT) {
                    ++stageCount;
                }
            }

            vector<const char*> sourceCodeCstr;
            if (stageCount == 1) {
                isSeparable = true;
                U8 index = 0u;
                for (const LoadData& it : _loadData._data) {
                    if (it._type != ShaderType::COUNT) {
                        break;
                    }
                    ++index;
                }

                const U8 shaderIdx = to_base(GetShaderType(_stageMask));
                const LoadData& data = _loadData._data[index];

                eastl::transform(cbegin(data.sourceCode), cend(data.sourceCode), back_inserter(sourceCodeCstr), std::mem_fn(&eastl::string::c_str));
                if_constexpr(Config::ENABLE_GPU_VALIDATION) {
                    shaderCompileTimerGPU.start();
                }
                if (_programHandle != GLUtil::k_invalidObjectID) {
                    GL_API::DeleteShaderPrograms(1, &_programHandle);
                }
                if_constexpr(Config::ENABLE_GPU_VALIDATION) {
                    shaderCompileDriveSide.start();
                }
                _programHandle = glCreateShaderProgramv(GLUtil::glShaderStageTable[shaderIdx], static_cast<GLsizei>(sourceCodeCstr.size()), sourceCodeCstr.data());
                if_constexpr(Config::ENABLE_GPU_VALIDATION) {
                    programCompileTimeGPU = getTimerAndReset(shaderCompileTimerGPU);
                    programCompileTimeDriver[0u] = getTimerAndReset(shaderCompileDriveSide);
                }
                if (_programHandle == 0 || _programHandle == GLUtil::k_invalidObjectID) {
                    Console::errorfn(Locale::Get(_ID("ERROR_GLSL_CREATE_PROGRAM")), _name.c_str());
                    _valid = false;
                    return ShaderResult::Failed;
                }

            } else {
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

                for (U8 i = 0u; i < to_base(ShaderType::COUNT); ++i) {
                    const LoadData& data = _loadData._data[i];

                    sourceCodeCstr.resize(0);
                    eastl::transform(cbegin(data.sourceCode), cend(data.sourceCode), back_inserter(sourceCodeCstr), std::mem_fn(&eastl::string::c_str));

                    if (!sourceCodeCstr.empty()) {
                        if_constexpr(Config::ENABLE_GPU_VALIDATION) {
                            shaderCompileTimerGPU.start();
                        }
                        const GLuint shader = glCreateShader(GLUtil::glShaderStageTable[i]);
                        if (shader != 0u) {
                            if_constexpr(Config::ENABLE_GPU_VALIDATION) {
                                shaderCompileDriveSide.start();
                            }
                            glShaderSource(shader, static_cast<GLsizei>(sourceCodeCstr.size()), sourceCodeCstr.data(), nullptr);
                            glCompileShader(shader);
                            if_constexpr(Config::ENABLE_GPU_VALIDATION) {
                                programCompileTimeDriver[i] = getTimerAndReset(shaderCompileDriveSide);
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

                                Console::errorfn(Locale::Get(_ID("ERROR_GLSL_COMPILE")), _name.c_str(), shader, Names::shaderTypes[i], validationBuffer.c_str());

                                glDeleteShader(shader);
                            } else {
                                _shaderIDs[i] = shader;
                                glAttachShader(_programHandle, shader);
                                shouldLink = true;
                            }
                        }
                        if_constexpr(Config::ENABLE_GPU_VALIDATION) {
                            stageCompileTimeGPU[i] = getTimerAndReset(shaderCompileTimerGPU);
                            programCompileTimeGPU += stageCompileTimeGPU[i];
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
        }

        if (_valid) {
            if_constexpr(Config::ENABLE_GPU_VALIDATION) {
                shaderCompileTimerGPU.start();
            }

            if_constexpr(glShaderProgram::g_useUniformConstantBuffer) {
                glBufferedPushConstantUploaderDescriptor bufferDescriptor = {};
                bufferDescriptor._programHandle = _programHandle;
                bufferDescriptor._uniformBufferName = GetUniformBufferName(this).c_str();
                bufferDescriptor._parentShaderName = _name.c_str();
                bufferDescriptor._blockIndex = blockIndex;
                _constantUploader = eastl::make_unique<glBufferedPushConstantUploader>(bufferDescriptor);
            } else {
                _constantUploader = eastl::make_unique<glUniformPushConstantUploader>(_programHandle);
            }

            _constantUploader->cacheUniforms();

            if_constexpr(Config::ENABLE_GPU_VALIDATION) {
                uniformCaching = getTimerAndReset(shaderCompileTimerGPU);
                programCompileTimeGPU += uniformCaching;
                programCompileTimeGPUMisc += uniformCaching;
            }
        }

        if_constexpr(Config::ENABLE_GPU_VALIDATION) {
            totalCompileTime = getTimerAndReset(shaderCompileTimer);

            if (isSeparable) {
                Console::printfn(Locale::Get(_ID("SHADER_COMPILE_TIME_SEPARABLE")),
                                 name().c_str(),
                                 parentProgramHandle,
                                 Time::MicrosecondsToMilliseconds<F32>(totalCompileTime),
                                 Time::MicrosecondsToMilliseconds<F32>(programCompileTimeGPU),
                                 Time::MicrosecondsToMilliseconds<F32>(programCompileTimeDriver[0u]),
                                 Time::MicrosecondsToMilliseconds<F32>(uniformCaching),
                                 Time::MicrosecondsToMilliseconds<F32>(programLogRetrieval));
            } else {
                U8 maxStageGPU = 0u; U64 maxTimeGPU = 0u;
                U8 maxStageDriver = 0u; U64 maxTimeDriver = 0u;
                U64 totalTimeDriver = 0u;

                string perStageTiming = "";
                for (U8 i = 0u; i < to_base(ShaderType::COUNT); ++i) {
                    if (_loadData._data[i]._type == ShaderType::COUNT) {
                        continue;
                    }

                    const U64 gpu = stageCompileTimeGPU[i];
                    const U64 driver = programCompileTimeDriver[i];

                    if (gpu > maxTimeGPU) {
                        maxStageGPU = i;
                        maxTimeGPU = gpu;
                    }
                    if (driver > maxTimeDriver) {
                        maxStageDriver = i;
                        maxTimeDriver = driver;
                    }
                    totalTimeDriver += driver;

                    perStageTiming.append(Util::StringFormat("---- [ %s ] - [%5.2f] - [%5.2f]\n", 
                                          Names::shaderTypes[i],
                                          Time::MicrosecondsToMilliseconds<F32>(gpu),
                                          Time::MicrosecondsToMilliseconds<F32>(driver)));

                    ScopedLock<SharedMutex> w_lock(s_hotspotShaderLock);
                    if (maxTimeGPU > s_hotspotShaderGPU._hotspotShaderTimer) {
                        s_hotspotShaderGPU._hotspotShaderTimer = maxTimeGPU;
                        s_hotspotShaderGPU._hotspotShader = _loadData._data[i]._name.c_str();
                    }
                    if (maxTimeDriver > s_hotspotShaderDriver._hotspotShaderTimer) {
                        s_hotspotShaderDriver._hotspotShaderTimer = maxTimeDriver;
                        s_hotspotShaderDriver._hotspotShader = _loadData._data[i]._name.c_str();
                    }
                }
                const U64 maxPerStage = std::max(maxTimeGPU, maxTimeDriver);

                const bool isLogHotspot = programLogRetrieval > std::max(maxPerStage, driverLinkTime);
                const char* bottleneckGlobal = isLogHotspot ? "Log Retrieval / First use" : (maxPerStage > driverLinkTime ? "Compilation" : "Linking");
                const char* bottleneckPerStage = maxTimeGPU > maxTimeDriver ? "GPU" : "Driver";

                SharedLock<SharedMutex> w_lock(s_hotspotShaderLock);
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
                                 s_hotspotShaderDriver._hotspotShader.c_str(), Time::MicrosecondsToMilliseconds<F32>(s_hotspotShaderDriver._hotspotShaderTimer)
                                );
            }
        }
    }

    return _valid ? ShaderResult::OK : ShaderResult::Failed;
}

bool glShader::load(ShaderLoadData&& data) {
    bool hasSourceCode = false;

    _valid = false;
    _stageMask = UseProgramStageMask::GL_NONE_BIT;
    _loadData = MOV(data);

    const GLuint blockIndex = to_U32(ShaderBufferLocation::UNIFORM_BLOCK) + _loadData._uniformIndex;
    const string uniformBlock = Util::StringFormat(_loadData._uniformBlock, blockIndex, GetUniformBufferName(this));

    for (LoadData& it : _loadData._data) {
        if (it._type != ShaderType::COUNT) {
            _stageMask |= GetStageMask(it._type);
            for (auto& source : it.sourceCode) {
                if (!source.empty()) {
                    Util::ReplaceStringInPlace(source, "_CUSTOM_UNIFORMS__", it._hasUniforms ? uniformBlock : "");
                    hasSourceCode = true;
                }
            }
        }
    }

    if (!hasSourceCode) {
        Console::errorfn(Locale::Get(_ID("ERROR_GLSL_NOT_FOUND")), name().c_str());
        return false;
    }

    string concatSource;
    for (const LoadData& it : _loadData._data) {
        if (it._type == ShaderType::COUNT) {
            continue;
        }

        concatSource.resize(0);
        for (const auto& src : it.sourceCode) {
            concatSource.append(src.c_str());
        }
        ShaderProgram::QueueShaderWriteToFile(concatSource, it._fileName);
    }

    return true;
}

// ============================ static data =========================== //
/// Remove a shader entity. The shader is deleted only if it isn't referenced by a program
void glShader::removeShader(glShader* s) {
    assert(s != nullptr);

    // Try to find it
    const U64 nameHash = s->nameHash();
    ScopedLock<SharedMutex> w_lock(_shaderNameLock);
    const ShaderMap::iterator it = _shaderNameMap.find(nameHash);
    if (it != std::end(_shaderNameMap)) {
        // Subtract one reference from it.
        if (s->SubRef()) {
            // If the new reference count is 0, delete the shader (as in leave it in the object arena)
            _shaderNameMap.erase(nameHash);
        }
    }
}

/// Return a new shader reference
glShader* glShader::getShader(const Str256& name) {
    // Try to find the shader
    SharedLock<SharedMutex> r_lock(_shaderNameLock);
    const ShaderMap::iterator it = _shaderNameMap.find(_ID(name.c_str()));
    if (it != std::end(_shaderNameMap)) {
        return it->second;
    }

    return nullptr;
}

/// Load a shader by name, source code and stage
glShader* glShader::loadShader(GFXDevice& context,
                               const Str256& name,
                               ShaderLoadData&& data) {
    // See if we have the shader already loaded
    glShader* shader = getShader(name);

    bool newShader = false;
    // If we do, and don't need a recompile, just return it
    if (shader == nullptr) {
        // If we can't find it, we create a new one
        ScopedLock<Mutex> w_lock(context.objectArenaMutex());
        shader = new (context.objectArena()) glShader(context, name);
        context.objectArena().DTOR(shader);
        newShader = true;
    }

    return loadShader(shader, newShader, MOV(data));
}


glShader* glShader::loadShader(glShader * shader,
                               const bool isNew,
                               ShaderLoadData&& data) {

    // At this stage, we have a valid Shader object, so load the source code
    if (shader->load(MOV(data))) {
        if (isNew) {
            // If we loaded the source code successfully,  register it
            ScopedLock<SharedMutex> w_lock(_shaderNameLock);
            _shaderNameMap.insert({ shader->nameHash(), shader });
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
    if (valid()) {
        for (const GFX::PushConstant& constant : constants.data()) {
            _constantUploader->uploadPushConstant(constant);
        }
        _constantUploader->commit();
    }
}

void glShader::prepare() const {
    if (valid()) {
        _constantUploader->prepare();
    }
}

void glShader::onParentValidation() {
    for (const GLuint shader : _shaderIDs) {
        if (shader != GLUtil::k_invalidObjectID) {
            glDetachShader(_programHandle, shader);
            glDeleteShader(shader);
        }
    }
    _shaderIDs.fill(GLUtil::k_invalidObjectID);
}

} // namespace Divide
