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

#include <Vulkan/vulkan_core.h>

namespace Divide {
    class vkShader final : public ShaderModule {
    public:
        explicit vkShader(GFXDevice& context, const Str256& name);
        ~vkShader();

        [[nodiscard]] bool load(const ShaderProgram::LoadData& data);

        /// Add or refresh a shader from the cache
        [[nodiscard]] static vkShader* LoadShader(GFXDevice& context,
                                                  const Str256& name,
                                                  bool overwriteExisting,
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
        vkShaderProgram(GFXDevice& context,
            const size_t descriptorHash,
            const Str256& name,
            const Str256& assetName,
            const ResourcePath& assetLocation,
            const ShaderProgramDescriptor& descriptor,
            ResourceCache& parentCache);
        ~vkShaderProgram();

        static void Idle(PlatformContext& platformContext);

        const vector<vkShader*>& shaderStages() const { return _shaderStage; }

    protected:
        ShaderResult validatePreBind(bool rebind = true);
        /// Make sure this program is ready for deletion
        bool unload() override;
        bool recompile(bool& skipped) override;
        void threadedLoad(bool reloadExisting) override;
        /// Returns true if at least one shader linked successfully
        bool reloadShaders(hashMap<U64, PerFileShaderData>& fileData, bool reloadExisting) override;

    private:
       bool _validationQueued = false;
       bool _stagesBound = false;
       vector<vkShader*> _shaderStage;
    };


} //namespace Divide

#endif // namespace VK_SHADER_PROGRAM_H