#include "stdafx.h"

#include "Headers/vkShaderProgram.h"

#include "Platform/Headers/PlatformRuntime.h"
#include "Platform/Video/RenderBackend/Vulkan/Headers/VKWrapper.h"

#include "Utility/Headers/Localization.h"

namespace Divide {

    vkShader::vkShader(GFXDevice& context, const Str256& name)
        : ShaderModule(context, name)
    {
        std::atomic_init(&_refCount, 0u);
    }

    vkShader::~vkShader()
    {
        reset();
    }
    
    void vkShader::reset()
    {
        if (_handle != VK_NULL_HANDLE)
        {
            vkDestroyShaderModule(VK_API::GetStateTracker()._device->getVKDevice(), _handle, nullptr);
            _handle = VK_NULL_HANDLE;
            _createInfo = {};
        }
    }

    /// Load a shader by name, source code and stage
    vkShader* vkShader::LoadShader(GFXDevice& context,
                                   const Str256& name,
                                   const bool overwriteExisting,
                                   ShaderProgram::LoadData& data)
    {
        ScopedLock<SharedMutex> w_lock(ShaderModule::s_shaderNameLock);

        // See if we have the shader already loaded
        ShaderModule* shader = GetShaderLocked(name);
        if (overwriteExisting && shader != nullptr)
        {
            RemoveShaderLocked(shader, true);
            shader = nullptr;
        }

        // If we do, and don't need a recompile, just return it
        if (shader == nullptr)
        {
            shader = MemoryManager_NEW vkShader(context, name);

            // If we loaded the source code successfully,  register it
            s_shaderNameMap.insert({ shader->nameHash(), shader });

            // At this stage, we have a valid Shader object, so load the source code
            if (!static_cast<vkShader*>(shader)->load(data))
            {
                DIVIDE_UNEXPECTED_CALL();
            }
        }
        else
        {
            shader->AddRef();
            Console::d_printfn(Locale::Get(_ID("SHADER_MANAGER_GET_INC")), shader->name().c_str(), shader->GetRef());
        }

        return static_cast<vkShader*>(shader);
    }

    bool vkShader::load(const ShaderProgram::LoadData& data)
    {
        _loadData = data;

        _valid = false;

        reset();

        _stageMask = VK_SHADER_STAGE_FLAG_BITS_MAX_ENUM;
        if (data._type != ShaderType::COUNT)
        {

            assert(_handle == VK_NULL_HANDLE);

            assert(!data._sourceCodeSpirV.empty());

            //create a new shader module, using the buffer we loaded
            const VkShaderModuleCreateInfo createInfo = vk::shaderModuleCreateInfo(data._sourceCodeSpirV.size() * sizeof(uint32_t), data._sourceCodeSpirV.data());

            VK_CHECK(vkCreateShaderModule(VK_API::GetStateTracker()._device->getVKDevice(), &createInfo, nullptr, &_handle));

            _createInfo = vk::pipelineShaderStageCreateInfo(vkShaderStageTable[to_base(data._type)], _handle);
            _stageMask = vkShaderStageTable[to_base(data._type)];
        }

        if (_stageMask == VK_SHADER_STAGE_FLAG_BITS_MAX_ENUM)
        {
            Console::errorfn(Locale::Get(_ID("ERROR_GLSL_NOT_FOUND")), name().c_str());
            return false;
        }

        return true;
    }

    void vkShaderProgram::Idle([[maybe_unused]] PlatformContext& platformContext)
    {
        PROFILE_SCOPE();

        assert(Runtime::isMainThread());
    }

    vkShaderProgram::vkShaderProgram(GFXDevice& context,
                                     const size_t descriptorHash,
                                     const Str256& name,
                                     const Str256& assetName,
                                     const ResourcePath& assetLocation,
                                     const ShaderProgramDescriptor& descriptor,
                                     ResourceCache& parentCache)
        : ShaderProgram(context, descriptorHash, name, assetName, assetLocation, descriptor, parentCache)
    {
    }

    vkShaderProgram::~vkShaderProgram()
    {
        if (!unload())
        {
            NOP();
        }
    }

    bool vkShaderProgram::unload() 
    {
        // Remove every shader attached to this program
        eastl::for_each(begin(_shaderStage),
                        end(_shaderStage),
                        [](vkShader* shader) 
                        {
                            ShaderModule::RemoveShader(shader);
                        });
        _shaderStage.clear();

        return ShaderProgram::unload();
    }

    const vector<vkShader*>& vkShaderProgram::shaderStages() const
    {
        return _shaderStage;
    }
    
    VkShaderStageFlags vkShaderProgram::stageMask() const 
    {
        VkShaderStageFlags ret{};
        for (const vkShader* shader : _shaderStage) 
        {
            ret |= shader->stageMask();
        }
        return ret;
    }

    ShaderResult vkShaderProgram::validatePreBind(const bool rebind)
    {
        PROFILE_SCOPE();

        return ShaderResult::OK;
    }

    bool vkShaderProgram::recompile(bool& skipped)
    {
        PROFILE_SCOPE();

        if (!ShaderProgram::recompile(skipped))
        {
            return false;
        }

        if (validatePreBind(false) != ShaderResult::OK)
        {
            return false;
        }

        skipped = false;

        threadedLoad(true);

        return true;
    }

    bool vkShaderProgram::reloadShaders(hashMap<U64, PerFileShaderData>& fileData, bool reloadExisting)
    {
        PROFILE_SCOPE();

        if (ShaderProgram::reloadShaders(fileData, reloadExisting))
        {
            _stagesBound = false;
            _shaderStage.clear();

            for (auto& [fileHash, loadDataPerFile] : fileData)
            {
                assert(!loadDataPerFile._modules.empty());

                for (auto& loadData : loadDataPerFile._loadData)
                {
                    if (loadData._type == ShaderType::COUNT)
                    {
                        continue;
                    }
                    
                    vkShader* shader = vkShader::LoadShader(_context, loadDataPerFile._programName, reloadExisting, loadData);
                    _shaderStage.push_back(shader);
                    _stagesBound = true;
                }
  
            }

            return !_shaderStage.empty();
        }

        return false;
    }

}; //namespace Divide
