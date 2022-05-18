#include "stdafx.h"

#include "Headers/vkShaderProgram.h"
#include "Platform/Video/RenderBackend/Vulkan/Headers/VKWrapper.h"

namespace Divide {
    namespace vkInit {
        VkPipelineShaderStageCreateInfo pipeline_shader_stage_create_info(VkShaderStageFlagBits stage, VkShaderModule shaderModule) {

            VkPipelineShaderStageCreateInfo info{};
            info.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            info.pNext = nullptr;

            //shader stage
            info.stage = stage;
            //module containing the code for this shader stage
            info.module = shaderModule;
            //the entry point of the shader
            info.pName = "main";
            return info;
        }
    }; //namespace vkInit

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
        unload();
    }

    bool vkShaderProgram::unload() {
        VKDevice* device = VK_API::GetStateTracker()->_device;

        // Remove every shader attached to this program
        eastl::for_each(begin(_shaderStage),
                         end(_shaderStage),
                  [&device](VkShaderModule& shader) {
                                vkDestroyShaderModule(device->getVKDevice(), shader, nullptr);
                          });
        _shaderStage.clear();

        return ShaderProgram::unload();
    }

    ShaderResult vkShaderProgram::validatePreBind(const bool rebind) {
        OPTICK_EVENT();

        return ShaderResult::OK;
    }

    bool vkShaderProgram::recompile(bool& skipped) {
        OPTICK_EVENT();

        if (!ShaderProgram::recompile(skipped)) {
            return false;
        }

        if (validatePreBind(false) != ShaderResult::OK) {
            return false;
        }

        skipped = false;

        threadedLoad(true);

        return true;
    }

    void vkShaderProgram::threadedLoad(bool reloadExisting) {
        OPTICK_EVENT()

        hashMap<U64, PerFileShaderData> loadDataByFile{};
        reloadShaders(loadDataByFile, reloadExisting);

        // Pass the rest of the loading steps to the parent class
        ShaderProgram::threadedLoad(reloadExisting);
    }
    
    bool vkShaderProgram::reloadShaders(hashMap<U64, PerFileShaderData>& fileData, bool reloadExisting) {
        OPTICK_EVENT();

        if (ShaderProgram::reloadShaders(fileData, reloadExisting)) {
            VKDevice* device = VK_API::GetStateTracker()->_device;

            _stagesBound = false;

            _shaderStage.clear();
            _shaderStagesCreateInfo.clear();
            for (auto& [fileHash, loadDataPerFile] : fileData) {
                assert(!loadDataPerFile._modules.empty());
                for (auto& loadData : loadDataPerFile._loadData) {
                    if (loadData._type == ShaderType::COUNT) {
                        continue;
                    }
                    assert(!loadData._sourceCodeSpirV.empty());

                    //create a new shader module, using the buffer we loaded
                    VkShaderModuleCreateInfo createInfo = {};
                    createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
                    createInfo.pNext = nullptr;

                    createInfo.codeSize = loadData._sourceCodeSpirV.size() * sizeof(uint32_t);
                    createInfo.pCode = loadData._sourceCodeSpirV.data();

                    auto& shaderModule = _shaderStage.emplace_back();
                    VK_CHECK(vkCreateShaderModule(device->getVKDevice(), &createInfo, nullptr, &shaderModule));

                    auto& pipelineInfo = _shaderStagesCreateInfo.emplace_back();
                    pipelineInfo = vkInit::pipeline_shader_stage_create_info(vkShaderStageTable[to_base(loadData._type)], shaderModule);
                }
  
            }
            return !_shaderStage.empty();
        }

        return false;
    }

}; //namespace Divide
