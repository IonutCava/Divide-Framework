#include "stdafx.h"

#include "Headers/glShader.h"

#include "Platform/File/Headers/FileManagement.h"
#include "Platform/Video/Headers/GFXDevice.h"
#include "Platform/Video/RenderBackend/OpenGL/Headers/glResources.h"
#include "Platform/Video/RenderBackend/OpenGL/Headers/GLWrapper.h"

#include "Core/Headers/StringHelper.h"
#include "Headers/glBufferedPushConstantUploader.h"
#include "Headers/glUniformPushConstantUploader.h"
#include "Utility/Headers/Localization.h"

namespace Divide {

namespace {

    size_t g_validationBufferMaxSize = 4096 * 16;

    ShaderType GetShaderType(const UseProgramStageMask mask) noexcept {
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

    UseProgramStageMask GetStageMask(const ShaderType type) noexcept {
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
}

glShader::~glShader() {
    if (_programHandle != GLUtil::k_invalidObjectID) {
        Console::d_printfn(Locale::Get(_ID("SHADER_DELETE")), name().c_str());
        GL_API::DeleteShaderPrograms(1, &_programHandle);
    }
}

bool glShader::uploadToGPU() {
    if (!_valid) {
        Console::d_printfn(Locale::Get(_ID("GLSL_LOAD_PROGRAM")), _name.c_str(), getGUID());

        const GLuint blockIndex = to_U32(ShaderBufferLocation::UNIFORM_BLOCK) + _loadData._uniformIndex;
        if (!loadFromBinary()) {

            U8 stageCount = 0u;
            for (const LoadData& it : _loadData._data) {
                if (it._type != ShaderType::COUNT) {
                    ++stageCount;
                }
            }

            if (stageCount == 1) {
                U8 index = 0u;
                for (const LoadData& it : _loadData._data) {
                    if (it._type != ShaderType::COUNT) {
                        break;
                    }
                    ++index;
                }

                const U8 shaderIdx = to_base(GetShaderType(_stageMask));
                const LoadData& data = _loadData._data[index];

                if (_programHandle != GLUtil::k_invalidObjectID) {
                    GL_API::DeleteShaderPrograms(1, &_programHandle);
                }

                vectorEASTL<const char*> sourceCodeCstr;
                eastl::transform(cbegin(data.sourceCode), cend(data.sourceCode), back_inserter(sourceCodeCstr), std::mem_fn(&eastl::string::c_str));
                _programHandle = glCreateShaderProgramv(GLUtil::glShaderStageTable[shaderIdx], static_cast<GLsizei>(sourceCodeCstr.size()), sourceCodeCstr.data());
                if (_programHandle == 0 || _programHandle == GLUtil::k_invalidObjectID) {
                    Console::errorfn(Locale::Get(_ID("ERROR_GLSL_CREATE_PROGRAM")), _name.c_str());
                    _valid = false;
                    return false;
                }

            } else {
                if (_programHandle == GLUtil::k_invalidObjectID) {
                    _programHandle = glCreateProgram();
                }
                if (_programHandle == 0 || _programHandle == GLUtil::k_invalidObjectID) {
                    Console::errorfn(Locale::Get(_ID("ERROR_GLSL_CREATE_PROGRAM")), _name.c_str());
                    _valid = false;
                    return false;
                }
                glProgramParameteri(_programHandle, GL_PROGRAM_BINARY_RETRIEVABLE_HINT, GL_TRUE);
                glProgramParameteri(_programHandle, GL_PROGRAM_SEPARABLE, GL_TRUE);

                bool shouldLink = false;
                std::array<GLuint, to_base(ShaderType::COUNT)> shaders = {};

                for (U8 i = 0; i < to_base(ShaderType::COUNT); ++i) {
                    const LoadData& data = _loadData._data[i];

                    vectorEASTL<const char*> sourceCodeCstr;
                    eastl::transform(cbegin(data.sourceCode), cend(data.sourceCode), back_inserter(sourceCodeCstr), std::mem_fn(&eastl::string::c_str));
                    if (!data.sourceCode.empty()) {
                        const GLuint shader = glCreateShader(GLUtil::glShaderStageTable[i]);
                        if (shader != 0u) {
                            glShaderSource(shader, static_cast<GLsizei>(sourceCodeCstr.size()), sourceCodeCstr.data(), nullptr);
                            glCompileShader(shader);

                            GLboolean compiled = 0;
                            glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
                            if (compiled == GL_FALSE) {
                                // error
                                GLint logSize = 0;
                                glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &logSize);
                                stringImpl validationBuffer;
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
                                shaders[i] = shader;
                                glAttachShader(_programHandle, shader);
                                shouldLink = true;
                            }
                        }
                    }
                }

                if (shouldLink) {
                    glLinkProgram(_programHandle);

                    for (const GLuint shader : shaders) {
                        if (shader != 0u) {
                            glDetachShader(_programHandle, shader);
                            glDeleteShader(shader);
                        }
                    }
                }
            }

            // And check the result
            GLboolean linkStatus = GL_FALSE;
            glGetProgramiv(_programHandle, GL_LINK_STATUS, &linkStatus);

            // If linking failed, show an error, else print the result in debug builds.
            if (linkStatus == GL_FALSE) {
                GLint logSize = 0;
                glGetProgramiv(_programHandle, GL_INFO_LOG_LENGTH, &logSize);
                stringImpl validationBuffer;
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
        }

        if (_valid) {
            if_constexpr(glShaderProgram::g_useUniformConstantBuffer) {
                glBufferedPushConstantUploaderDescriptor bufferDescriptor = {};
                bufferDescriptor._programHandle = _programHandle;
                bufferDescriptor._uniformBufferName = getUniformBufferName().c_str();
                bufferDescriptor._parentShaderName = _name.c_str();
                bufferDescriptor._blockIndex = blockIndex;
                _constantUploader = eastl::make_unique<glBufferedPushConstantUploader>(bufferDescriptor);
            } else {
                _constantUploader = eastl::make_unique<glUniformPushConstantUploader>(_programHandle);
            }

            _constantUploader->cacheUniforms();
        }
    }

    return _valid;
}

bool glShader::load(const ShaderLoadData& data) {
    bool hasSourceCode = false;

    _valid = false;
    _stageMask = UseProgramStageMask::GL_NONE_BIT;
    _loadData = data;

    const GLuint blockIndex = to_U32(ShaderBufferLocation::UNIFORM_BLOCK) + _loadData._uniformIndex;
    const stringImpl uniformBlock = Util::StringFormat(_loadData._uniformBlock, blockIndex, getUniformBufferName());

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

    stringImpl concatSource;
    for (const LoadData& it : _loadData._data) {
        concatSource.resize(0);
        for (const auto& src : it.sourceCode) {
            concatSource.append(src.c_str());
        }
        glShaderProgram::QueueShaderWriteToFile(concatSource, it._fileName);
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
                               const ShaderLoadData& data) {
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

    return loadShader(shader, newShader, data);
}


glShader* glShader::loadShader(glShader * shader,
                               const bool isNew,
                               const ShaderLoadData & data) {

    // At this stage, we have a valid Shader object, so load the source code
    if (shader->load(data)) {
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
        // Load the program's binary format from file
        vectorEASTL<Byte> data;
        if (readFile(Paths::g_cacheLocation + Paths::Shaders::g_cacheLocationBin,
                     ResourcePath((glShaderProgram::decorateFileName(_name) + ".fmt").c_str()),
                     data,
                     FileType::BINARY) == FileError::NONE &&
            !data.empty())
        {
            const GLenum binaryFormat = *reinterpret_cast<GLenum*>(data.data());
            if (binaryFormat != GL_NONE) {
                data.resize(0);
                if (readFile(Paths::g_cacheLocation + Paths::Shaders::g_cacheLocationBin,
                    ResourcePath((glShaderProgram::decorateFileName(_name) + ".bin").c_str()),
                             data,
                             FileType::BINARY) == FileError::NONE &&
                    !data.empty())
                {
                    // Load binary code on the GPU
                    _programHandle = glCreateProgram();
                    glProgramBinary(_programHandle, binaryFormat, (bufferPtr)data.data(), static_cast<GLint>(data.size()));
                    // Check if the program linked successfully on load
                    GLboolean success = 0;
                    glGetProgramiv(_programHandle, GL_LINK_STATUS, &success);
                    // If it loaded properly set all appropriate flags (this also prevents low level access to the program's shaders)
                    if (success == GL_TRUE) {
                        _loadedFromBinary = true;
                        _valid = true;
                    }
                }
            }
        }
    }

    return _loadedFromBinary;
}

bool glShader::DumpBinary(const GLuint handle, const Str256& name) {
    static eastl::set<GLuint> s_dumpedBinaries;
    if (!s_dumpedBinaries.insert(handle).second) {
        return false;
    }

    bool ret = false;
    // Get the size of the binary code
    GLint binaryLength = 0;
    glGetProgramiv(handle, GL_PROGRAM_BINARY_LENGTH, &binaryLength);
    // allocate a big enough buffer to hold it
    char* binary = MemoryManager_NEW char[binaryLength];
    DIVIDE_ASSERT(binary != nullptr, "glShader error: could not allocate memory for the program binary!");

    // and fill the buffer with the binary code
    GLenum binaryFormat = GL_NONE;
    glGetProgramBinary(handle, binaryLength, nullptr, &binaryFormat, binary);
    if (binaryFormat != GL_NONE) {
        // dump the buffer to file
        if (writeFile(Paths::g_cacheLocation + Paths::Shaders::g_cacheLocationBin,
            ResourcePath((glShaderProgram::decorateFileName(name) + ".bin").c_str()),
                      binary,
                      static_cast<size_t>(binaryLength),
                      FileType::BINARY) == FileError::NONE)
        {
            // dump the format to a separate file (highly non-optimised. Should dump formats to a database instead)
            if (writeFile(Paths::g_cacheLocation + Paths::Shaders::g_cacheLocationBin,
                ResourcePath((glShaderProgram::decorateFileName(name) + ".fmt").c_str()),
                          &binaryFormat,
                          sizeof(GLenum),
                          FileType::BINARY) == FileError::NONE)
            {
                ret = true;
            }
        }
    }

    // delete our local code buffer
    MemoryManager::DELETE(binary);
    return ret;
}


stringImpl glShader::getUniformBufferName() const noexcept {
    return "dvd_UniformBlock_" + Util::to_string(getGUID());
}


void glShader::uploadPushConstants(const PushConstants& constants) const {
    if (valid()) {
        for (const auto& constant : constants._data) {
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

/// Add a define to the shader. The defined must not have been added previously
void glShader::addShaderDefine(const stringImpl& define, bool appendPrefix) {
    // Find the string in the list of program defines
    const auto* it = std::find(std::begin(_definesList), std::end(_definesList), std::make_pair(define, appendPrefix));
    // If we can't find it, we add it
    if (it == std::end(_definesList)) {
        _definesList.emplace_back(define, appendPrefix);
        shouldRecompile(true);
    } else {
        // If we did find it, we'll show an error message in debug builds about double add
        Console::d_errorfn(Locale::Get(_ID("ERROR_INVALID_DEFINE_ADD")), define.c_str(), _name.c_str());
    }
}

/// Remove a define from the shader. The defined must have been added previously
void glShader::removeShaderDefine(const stringImpl& define) {
    // Find the string in the list of program defines
    const auto* it = eastl::find(eastl::begin(_definesList), eastl::end(_definesList), std::make_pair(define, true));
        if (it == eastl::end(_definesList)) {
            it = eastl::find(eastl::begin(_definesList), eastl::end(_definesList), std::make_pair(define, false));
        }
    // If we find it, we remove it
    if (it != eastl::end(_definesList)) {
        _definesList.erase(it);
        shouldRecompile(true);
    } else {
        // If we did not find it, we'll show an error message in debug builds
        Console::d_errorfn(Locale::Get(_ID("ERROR_INVALID_DEFINE_DELETE")), define.c_str(), _name.c_str());
    }
}

} // namespace Divide
