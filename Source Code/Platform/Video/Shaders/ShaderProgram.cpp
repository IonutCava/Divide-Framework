#include "stdafx.h"

#include "Headers/ShaderProgram.h"


#include "Core/Headers/Configuration.h"
#include "Core/Resources/Headers/ResourceCache.h"
#include "Geometry/Material/Headers/Material.h"
#include "Managers/Headers/SceneManager.h"
#include "Platform/Video/Buffers/ShaderBuffer/Headers/ShaderBuffer.h"
#include "Platform/Video/Headers/GFXDevice.h"

namespace Divide {
    bool ShaderProgram::s_useShaderTextCache = false;
    bool ShaderProgram::s_useShaderBinaryCache = false;


ShaderProgram_ptr ShaderProgram::s_imShader = nullptr;
ShaderProgram_ptr ShaderProgram::s_nullShader = nullptr;
ShaderProgram::ShaderQueue ShaderProgram::s_recompileQueue;
ShaderProgram::ShaderProgramMap ShaderProgram::s_shaderPrograms;
std::pair<I64, ShaderProgram::ShaderProgramMapEntry> ShaderProgram::s_lastRequestedShaderProgram = { -1, {} };

SharedMutex ShaderProgram::s_programLock;
std::atomic_int ShaderProgram::s_shaderCount;

void ProcessShadowMappingDefines(const Configuration& config, ModuleDefines& defines) {
    if (!config.rendering.shadowMapping.enabled) {
        defines.emplace_back("DISABLE_SHADOW_MAPPING", true);
    } else {
        if (!config.rendering.shadowMapping.csm.enabled) {
            defines.emplace_back("DISABLE_SHADOW_MAPPING_CSM", true);
        }
        if (!config.rendering.shadowMapping.spot.enabled) {
            defines.emplace_back("DISABLE_SHADOW_MAPPING_SPOT", true);
        }
        if (!config.rendering.shadowMapping.point.enabled) {
            defines.emplace_back("DISABLE_SHADOW_MAPPING_POINT", true);
        }
    }
}

size_t ShaderProgramDescriptor::getHash() const {
    _hash = PropertyDescriptor::getHash();
    for (const ShaderModuleDescriptor& desc : _modules) {
        Util::Hash_combine(_hash, ShaderProgram::DefinesHash(desc._defines));
        Util::Hash_combine(_hash, std::string(desc._variant.c_str()));
        Util::Hash_combine(_hash, desc._sourceFile.data());
        Util::Hash_combine(_hash, desc._moduleType);
        Util::Hash_combine(_hash, desc._batchSameFile);
    }
    return _hash;
}

ShaderProgram::ShaderProgram(GFXDevice& context, 
                             const size_t descriptorHash,
                             const Str256& shaderName,
                             const Str256& shaderFileName,
                             const ResourcePath& shaderFileLocation,
                             ShaderProgramDescriptor descriptor,
                             const bool asyncLoad)
    : CachedResource(ResourceType::GPU_OBJECT, descriptorHash, shaderName, ResourcePath(shaderFileName), shaderFileLocation),
      GraphicsResource(context, Type::SHADER_PROGRAM, getGUID(), _ID(shaderName.c_str())),
      _descriptor(MOV(descriptor)),
      _asyncLoad(asyncLoad)
{
    if (shaderFileName.empty()) {
        assetName(ResourcePath(resourceName().c_str()));
    }
    s_shaderCount.fetch_add(1, std::memory_order_relaxed);
}

ShaderProgram::~ShaderProgram()
{
    Console::d_printfn(Locale::Get(_ID("SHADER_PROGRAM_REMOVE")), resourceName().c_str());
    s_shaderCount.fetch_sub(1, std::memory_order_relaxed);
}

bool ShaderProgram::load() {
    return CachedResource::load();
}

bool ShaderProgram::unload() {
    // Unregister the program from the manager
    return UnregisterShaderProgram(descriptorHash());
}


/// Rebuild the specified shader stages from source code
bool ShaderProgram::recompile(bool force, bool& skipped) {
    skipped = true;
    return getState() == ResourceState::RES_LOADED;
}

//================================ static methods ========================================
void ShaderProgram::Idle() {
    OPTICK_EVENT();

    // If we don't have any shaders queued for recompilation, return early
    if (!s_recompileQueue.empty()) {
        // Else, recompile the top program from the queue
        bool skipped = false;
        if (s_recompileQueue.top()->recompile(true, skipped)) {
            if (!skipped) {
                if (s_recompileQueue.top()->getGUID() == s_lastRequestedShaderProgram.first) {
                    s_lastRequestedShaderProgram = { -1, {} };
                }
            }
        } else {
            DIVIDE_UNEXPECTED_CALL();
        }
        //Re-register because the handle is probably different by now
        RegisterShaderProgram(s_recompileQueue.top());
        s_recompileQueue.pop();
    }
}

/// Calling this will force a recompilation of all shader stages for the program
/// that matches the name specified
bool ShaderProgram::RecompileShaderProgram(const Str256& name) {
    bool state = false;
    SharedLock<SharedMutex> r_lock(s_programLock);

    // Find the shader program
    for (const auto& [handle, programEntry] : s_shaderPrograms) {
       
        ShaderProgram* program = programEntry.first;
        assert(program != nullptr);

        const Str256& shaderName = program->resourceName();
        // Check if the name matches any of the program's name components    
        if (shaderName.find(name) != Str256::npos || shaderName.compare(name) == 0) {
            // We process every partial match. So add it to the recompilation queue
            s_recompileQueue.push(program);
            // Mark as found
            state = true;
        }
    }
    // If no shaders were found, show an error
    if (!state) {
        Console::errorfn(Locale::Get(_ID("ERROR_RECOMPILE_NOT_FOUND")),  name.c_str());
    }

    return state;
}

void ShaderProgram::OnStartup(ResourceCache* parentCache) {

    ShaderModuleDescriptor vertModule = {};
    vertModule._moduleType = ShaderType::VERTEX;
    vertModule._sourceFile = "ImmediateModeEmulation.glsl";

    ShaderModuleDescriptor fragModule = {};
    fragModule._moduleType = ShaderType::FRAGMENT;
    fragModule._sourceFile = "ImmediateModeEmulation.glsl";

    ShaderProgramDescriptor shaderDescriptor = {};
    shaderDescriptor._modules.push_back(vertModule);
    shaderDescriptor._modules.push_back(fragModule);

    // Create an immediate mode rendering shader that simulates the fixed function pipeline
    ResourceDescriptor immediateModeShader("ImmediateModeEmulation");
    immediateModeShader.threaded(false);
    immediateModeShader.propertyDescriptor(shaderDescriptor);
    s_imShader = CreateResource<ShaderProgram>(parentCache, immediateModeShader);
    assert(s_imShader != nullptr);

    shaderDescriptor._modules.clear();
    ResourceDescriptor shaderDesc("NULL");
    shaderDesc.threaded(false);
    shaderDesc.propertyDescriptor(shaderDescriptor);
    // Create a null shader (basically telling the API to not use any shaders when bound)
    s_nullShader = CreateResource<ShaderProgram>(parentCache, shaderDesc);
    // The null shader should never be nullptr!!!!
    assert(s_nullShader != nullptr);  // LoL -Ionut
}

void ShaderProgram::OnShutdown() {
    // Make sure we unload all shaders
    s_nullShader.reset();
    s_imShader.reset();
    while (!s_recompileQueue.empty()) {
        s_recompileQueue.pop();
    }
    s_shaderPrograms.clear();
    s_lastRequestedShaderProgram = { -1, {} };
}

bool ShaderProgram::UpdateAll() {
    OPTICK_EVENT();

    static bool onOddFrame = false;

    onOddFrame = !onOddFrame;
    if_constexpr(!Config::Build::IS_RELEASE_BUILD) {
        if (onOddFrame) {
            SharedLock<SharedMutex> r_lock(s_programLock);
            bool skipped = false;
            for (const auto& [handle, programEntry] : s_shaderPrograms) {
                programEntry.first->recompile(false, skipped);
                if (!skipped && handle == s_lastRequestedShaderProgram.first) {
                    s_lastRequestedShaderProgram = {-1, {}};
                }
            }
        }
    }

    return true;
}

/// Whenever a new program is created, it's registered with the manager
void ShaderProgram::RegisterShaderProgram(ShaderProgram* shaderProgram) {
    size_t shaderHash = shaderProgram->descriptorHash();
    UnregisterShaderProgram(shaderHash);

    ScopedLock<SharedMutex> w_lock(s_programLock);
    s_shaderPrograms[shaderProgram->getGUID()] = { shaderProgram, shaderHash };
}

/// Unloading/Deleting a program will unregister it from the manager
bool ShaderProgram::UnregisterShaderProgram(size_t shaderHash) {

    ScopedLock<SharedMutex> w_lock(s_programLock);
    s_lastRequestedShaderProgram = { -1, {} };

    if (s_shaderPrograms.empty()) {
        // application shutdown?
        return true;
    }

    const ShaderProgramMap::const_iterator it = eastl::find_if(
        eastl::cbegin(s_shaderPrograms),
        eastl::cend(s_shaderPrograms),
        [shaderHash](const ShaderProgramMap::value_type& item) {
            return item.second.second == shaderHash;
        });

    if (it != eastl::cend(s_shaderPrograms)) {
        if (it->first == s_lastRequestedShaderProgram.first) {
            s_lastRequestedShaderProgram = { -1, {} };
        }

        s_shaderPrograms.erase(it);
        return true;
    }

    // application shutdown?
    return false;
}

ShaderProgram* ShaderProgram::FindShaderProgram(const I64 shaderHandle) {
    SharedLock<SharedMutex> r_lock(s_programLock);
    if (shaderHandle == s_lastRequestedShaderProgram.first) {
        return s_lastRequestedShaderProgram.second.first;
    }

    const auto& it = s_shaderPrograms.find(shaderHandle);
    if (it != eastl::cend(s_shaderPrograms)) {
        s_lastRequestedShaderProgram = { shaderHandle, it->second };

        return it->second.first;
    }
    s_lastRequestedShaderProgram = { -1, {} };
    return nullptr;
}

ShaderProgram* ShaderProgram::FindShaderProgram(const size_t shaderHash) {
    SharedLock<SharedMutex> r_lock(s_programLock);
    if (s_lastRequestedShaderProgram.first != -1 && s_lastRequestedShaderProgram.second.second == shaderHash) {
        return s_lastRequestedShaderProgram.second.first;
    }

    for (const auto& [handle, programEntry] : s_shaderPrograms) {
        if (programEntry.second == shaderHash) {
            s_lastRequestedShaderProgram = { handle, programEntry };

            return programEntry.first;
        }
    }

    s_lastRequestedShaderProgram = { -1, {} };
    return nullptr;
}

const ShaderProgram_ptr& ShaderProgram::DefaultShader() noexcept {
    return s_imShader;
}

const ShaderProgram_ptr& ShaderProgram::NullShader() noexcept {
    return s_nullShader;
}

const I64 ShaderProgram::NullShaderGUID() noexcept {
    return s_nullShader != nullptr ? s_nullShader->getGUID() : -1;
}

void ShaderProgram::RebuildAllShaders() {
    SharedLock<SharedMutex> r_lock(s_programLock);
    for (const auto& [handle, programEntry] : s_shaderPrograms) {
        s_recompileQueue.push(programEntry.first);
    }
}

vector<ResourcePath> ShaderProgram::GetAllAtomLocations() {
    static vector<ResourcePath> atomLocations;
    if (atomLocations.empty()) {
        // General
        atomLocations.emplace_back(Paths::g_assetsLocation +
                                   Paths::g_shadersLocation);
        // GLSL
        atomLocations.emplace_back(Paths::g_assetsLocation +
                                   Paths::g_shadersLocation +
                                   Paths::Shaders::GLSL::g_parentShaderLoc);

        atomLocations.emplace_back(Paths::g_assetsLocation +
                                   Paths::g_shadersLocation +
                                   Paths::Shaders::GLSL::g_parentShaderLoc +
                                   Paths::Shaders::GLSL::g_comnAtomLoc);

        atomLocations.emplace_back(Paths::g_assetsLocation +
                                   Paths::g_shadersLocation +
                                   Paths::Shaders::GLSL::g_parentShaderLoc +
                                   Paths::Shaders::GLSL::g_compAtomLoc);

        atomLocations.emplace_back(Paths::g_assetsLocation +
                                   Paths::g_shadersLocation +
                                   Paths::Shaders::GLSL::g_parentShaderLoc +
                                   Paths::Shaders::GLSL::g_fragAtomLoc);

        atomLocations.emplace_back(Paths::g_assetsLocation +
                                   Paths::g_shadersLocation +
                                   Paths::Shaders::GLSL::g_parentShaderLoc +
                                   Paths::Shaders::GLSL::g_geomAtomLoc);

        atomLocations.emplace_back(Paths::g_assetsLocation +
                                   Paths::g_shadersLocation +
                                   Paths::Shaders::GLSL::g_parentShaderLoc +
                                   Paths::Shaders::GLSL::g_tescAtomLoc);

        atomLocations.emplace_back(Paths::g_assetsLocation +
                                   Paths::g_shadersLocation +
                                   Paths::Shaders::GLSL::g_parentShaderLoc +
                                   Paths::Shaders::GLSL::g_teseAtomLoc);

        atomLocations.emplace_back(Paths::g_assetsLocation +
                                   Paths::g_shadersLocation +
                                   Paths::Shaders::GLSL::g_parentShaderLoc +
                                   Paths::Shaders::GLSL::g_vertAtomLoc);
        // HLSL
        atomLocations.emplace_back(Paths::g_assetsLocation +
                                   Paths::g_shadersLocation +
                                   Paths::Shaders::HLSL::g_parentShaderLoc);

    }

    return atomLocations;
}

size_t ShaderProgram::DefinesHash(const ModuleDefines& defines) {
    if (defines.empty()) {
        return 0;
    }

    size_t hash = 7919;
    for (const auto& [defineString, appendPrefix] : defines) {
        Util::Hash_combine(hash, _ID(defineString.c_str()));
        Util::Hash_combine(hash, appendPrefix);
    }
    return hash;
}

};
