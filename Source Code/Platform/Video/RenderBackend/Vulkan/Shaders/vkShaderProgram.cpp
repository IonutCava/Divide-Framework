#include "stdafx.h"

#include "Headers/vkShaderProgram.h"

#include "Platform/Headers/PlatformRuntime.h"
#include "Platform/Video/Headers/GFXDevice.h"
#include "Platform/Video/RenderBackend/Vulkan/Headers/VKWrapper.h"

#include "Utility/Headers/Localization.h"

namespace Divide {

    vkShader::vkShader(GFXDevice& context, const Str256& name, const U32 generation)
        : ShaderModule(context, name, generation)
    {
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
    vkShaderEntry vkShader::LoadShader(GFXDevice& context,
                                       vkShaderProgram* parent,
                                       const U32 targetGeneration,
                                       ShaderProgram::LoadData& data)
    {

        vkShaderEntry ret
        {
            ._fileHash = _ID( data._shaderName.c_str() ),
            ._generation = targetGeneration
        };
        {
            // If we loaded the source code successfully,  register it
            LockGuard<SharedMutex> w_lock(ShaderModule::s_shaderNameLock);
            auto& shader_ptr = s_shaderNameMap[ret._fileHash];
            if (shader_ptr == nullptr || shader_ptr->generation() < ret._generation )
            {
                shader_ptr.reset( new vkShader( context, data._shaderName, ret._generation ));
                // At this stage, we have a valid Shader object, so load the source code
                if (!static_cast<vkShader*>(shader_ptr.get())->load(data))
                {
                    DIVIDE_UNEXPECTED_CALL();
                }
            }
            else
            {
                Console::d_printfn(Locale::Get(_ID("SHADER_MANAGER_GET_INC")), shader_ptr->name().c_str());
            }

            ret._shader = static_cast<vkShader*>(shader_ptr.get());
        }

        ret._shader->registerParent( parent );
        return ret;
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

            Debug::SetObjectName( VK_API::GetStateTracker()._device->getVKDevice(), (uint64_t)_handle, VK_OBJECT_TYPE_SHADER_MODULE, name().c_str());

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
        PROFILE_SCOPE_AUTO( Profiler::Category::Graphics );

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
        for ( vkShaderEntry& shader : _shaderStage )
        {
            shader._shader->deregisterParent(this);
        }

        _shaderStage.clear();

        return ShaderProgram::unload();
    }

    const vkShaderProgram::vkShaders& vkShaderProgram::shaderStages() const noexcept
    {
        return _shaderStage;
    }
    
    VkShaderStageFlags vkShaderProgram::stageMask() const noexcept
    {
        VkShaderStageFlags ret{};
        for (const vkShaderEntry& shader : _shaderStage)
        {
            ret |= shader._shader->stageMask();
        }
        return ret;
    }

    ShaderResult vkShaderProgram::validatePreBind( const bool rebind )
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Graphics );
        return ShaderProgram::validatePreBind( rebind );
    }

    bool vkShaderProgram::loadInternal(hashMap<U64, PerFileShaderData>& fileData, bool overwrite)
    {
        PROFILE_SCOPE_AUTO( Profiler::Category::Graphics );

        if (ShaderProgram::loadInternal(fileData, overwrite))
        {
            for (auto& [fileHash, loadDataPerFile] : fileData)
            {
                assert(!loadDataPerFile._modules.empty());

                for (auto& loadData : loadDataPerFile._loadData)
                {
                    if (loadData._type == ShaderType::COUNT)
                    {
                        continue;
                    }

                    bool found = false;
                    U32 targetGeneration = 0u;
                    for ( vkShaderEntry& stage : _shaderStage )
                    {
                        if ( stage._fileHash == _ID( loadData._shaderName.c_str() ) )
                        {
                            targetGeneration = overwrite ? stage._generation + 1u : stage._generation;
                            stage = vkShader::LoadShader( _context, this, targetGeneration, loadData );
                            found = true;
                            break;
                        }
                    }
                    if ( !found )
                    {
                        _shaderStage.push_back( vkShader::LoadShader( _context, this, targetGeneration, loadData ) );
                    }
                }
  
            }

            if ( !_shaderStage.empty() )
            {
                VK_API::OnShaderReloaded(this);
                return true;
            }
        }

        return false;
    }

}; //namespace Divide
