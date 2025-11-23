/*
   Copyright (c) 2018 DIVIDE-Studio
   Copyright (c) 2009 Ionut Cava

   This file is part of DIVIDE Framework.

   Permission is hereby granted, free of charge, to any person obtaining a copy
   of this software
   and associated documentation files (the "Software"), to deal in the Software
   without restriction,
   including without limitation the rights to use, copy, modify, merge, publish,
   distribute, sublicense,
   and/or sell copies of the Software, and to permit persons to whom the
   Software is furnished to do so,
   subject to the following conditions:

   The above copyright notice and this permission notice shall be included in
   all copies or substantial portions of the Software.

   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
   IMPLIED,
   INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
   PARTICULAR PURPOSE AND NONINFRINGEMENT.
   IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
   DAMAGES OR OTHER LIABILITY,
   WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR
   IN CONNECTION WITH THE SOFTWARE
   OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

 */

#pragma once
#ifndef VK_SHADER_PROGRAM_H
#define VK_SHADER_PROGRAM_H

#include "Platform/Video/Shaders/Headers/ShaderProgram.h"
#include "Platform/Video/RenderBackend/Vulkan/Headers/vkResources.h"

#include <vulkan/vulkan_core.h>

namespace Divide {
    class vkShader;

    struct vkShaderEntry
    {
        vkShader* _shader{ nullptr };
        U64 _fileHash{ 0u };
        U32 _generation{ 0u };
    };

    class vkShader final : public ShaderModule {
    public:
        explicit vkShader(GFXDevice& context, const std::string_view name, U32 generation);
        ~vkShader() override;

        [[nodiscard]] bool load(const ShaderProgram::LoadData& data);

        /// Add or refresh a shader from the cache
        [[nodiscard]] static vkShaderEntry LoadShader(GFXDevice& context,
                                                      vkShaderProgram* parent,
                                                      U32 targetGeneration,
                                                      ShaderProgram::LoadData& data);

        PROPERTY_R_IW(VkShaderStageFlagBits, stageMask, VK_SHADER_STAGE_FLAG_BITS_MAX_ENUM);
        PROPERTY_R_IW(VkShaderModule, handle, VK_NULL_HANDLE);

    private:
        void reset();

    private:
        VkPipelineShaderStageCreateInfo _createInfo{};
        ShaderProgram::LoadData _loadData;
    };

    class vkShaderProgram final : public ShaderProgram {
    public:
        using vkShaders = fixed_vector<vkShaderEntry, to_base( ShaderType::COUNT )>;
    public:
        vkShaderProgram( PlatformContext& context, const ResourceDescriptor<ShaderProgram>& descriptor );
        ~vkShaderProgram() override;

        [[nodiscard]] const vkShaders& shaderStages() const noexcept;
        [[nodiscard]] VkShaderStageFlags stageMask() const noexcept;
        [[nodiscard]] ShaderResult validatePreBind( const bool rebind ) override;

        PROPERTY_RW( VkDescriptorSetLayout, descriptorSetLayout, VK_NULL_HANDLE);
        PROPERTY_RW( DynamicBindings, dynamicBindings);

    protected:
        /// Make sure this program is ready for deletion
        [[nodiscard]] bool unload() override;
        /// Returns true if at least one shader linked successfully
        [[nodiscard]] bool loadInternal(hashMap<U64, PerFileShaderData>& fileData, bool overwrite) override;

    private:
       vkShaders _shaderStage;
    };


} //namespace Divide

#endif // namespace VK_SHADER_PROGRAM_H
