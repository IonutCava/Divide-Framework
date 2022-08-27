#include "stdafx.h"

#include "Headers/GLSLToSPIRV.h"
#include "Platform/Video/Shaders/Headers/ShaderProgram.h"

#include <glslang/SPIRV/GlslangToSpv.h>
#include <Vulkan/vulkan.hpp>
#include <SPIRV-Reflect/spirv_reflect.h>

namespace {
    Divide::GFX::PushConstantType GetGFXType(const  glslang::TBasicType type) {
        switch (type) {
            default: Divide::DIVIDE_UNEXPECTED_CALL(); break;
            case glslang::TBasicType::EbtFloat:  return Divide::GFX::PushConstantType::FLOAT;
            case glslang::TBasicType::EbtDouble: return Divide::GFX::PushConstantType::DOUBLE;
            case glslang::TBasicType::EbtInt:    return Divide::GFX::PushConstantType::INT;
            case glslang::TBasicType::EbtUint:   return Divide::GFX::PushConstantType::UINT;
        }

        return Divide::GFX::PushConstantType::COUNT;
    }
};

bool SpirvHelper::s_isInit = false;
void SpirvHelper::Init() {
    assert(!s_isInit);

    glslang::InitializeProcess();
    s_isInit = true;
}

void SpirvHelper::Finalize() {
    if (s_isInit) {
        glslang::FinalizeProcess();
        s_isInit = false;
    }
}

void SpirvHelper::InitResources(TBuiltInResource& Resources) {
    Resources.maxLights = 32;
    Resources.maxClipPlanes = 6;
    Resources.maxTextureUnits = 32;
    Resources.maxTextureCoords = 32;
    Resources.maxVertexAttribs = 64;
    Resources.maxVertexUniformComponents = 4096;
    Resources.maxVaryingFloats = 64;
    Resources.maxVertexTextureImageUnits = 32;
    Resources.maxCombinedTextureImageUnits = 80;
    Resources.maxTextureImageUnits = 32;
    Resources.maxFragmentUniformComponents = 4096;
    Resources.maxDrawBuffers = 32;
    Resources.maxVertexUniformVectors = 128;
    Resources.maxVaryingVectors = 8;
    Resources.maxFragmentUniformVectors = 16;
    Resources.maxVertexOutputVectors = 16;
    Resources.maxFragmentInputVectors = 15;
    Resources.minProgramTexelOffset = -8;
    Resources.maxProgramTexelOffset = 7;
    Resources.maxClipDistances = 8;
    Resources.maxComputeWorkGroupCountX = 65535;
    Resources.maxComputeWorkGroupCountY = 65535;
    Resources.maxComputeWorkGroupCountZ = 65535;
    Resources.maxComputeWorkGroupSizeX = 1024;
    Resources.maxComputeWorkGroupSizeY = 1024;
    Resources.maxComputeWorkGroupSizeZ = 64;
    Resources.maxComputeUniformComponents = 1024;
    Resources.maxComputeTextureImageUnits = 16;
    Resources.maxComputeImageUniforms = 8;
    Resources.maxComputeAtomicCounters = 8;
    Resources.maxComputeAtomicCounterBuffers = 1;
    Resources.maxVaryingComponents = 60;
    Resources.maxVertexOutputComponents = 64;
    Resources.maxGeometryInputComponents = 64;
    Resources.maxGeometryOutputComponents = 128;
    Resources.maxFragmentInputComponents = 128;
    Resources.maxImageUnits = 8;
    Resources.maxCombinedImageUnitsAndFragmentOutputs = 8;
    Resources.maxCombinedShaderOutputResources = 8;
    Resources.maxImageSamples = 0;
    Resources.maxVertexImageUniforms = 0;
    Resources.maxTessControlImageUniforms = 0;
    Resources.maxTessEvaluationImageUniforms = 0;
    Resources.maxGeometryImageUniforms = 0;
    Resources.maxFragmentImageUniforms = 8;
    Resources.maxCombinedImageUniforms = 8;
    Resources.maxGeometryTextureImageUnits = 16;
    Resources.maxGeometryOutputVertices = 256;
    Resources.maxGeometryTotalOutputComponents = 1024;
    Resources.maxGeometryUniformComponents = 1024;
    Resources.maxGeometryVaryingComponents = 64;
    Resources.maxTessControlInputComponents = 128;
    Resources.maxTessControlOutputComponents = 128;
    Resources.maxTessControlTextureImageUnits = 16;
    Resources.maxTessControlUniformComponents = 1024;
    Resources.maxTessControlTotalOutputComponents = 4096;
    Resources.maxTessEvaluationInputComponents = 128;
    Resources.maxTessEvaluationOutputComponents = 128;
    Resources.maxTessEvaluationTextureImageUnits = 16;
    Resources.maxTessEvaluationUniformComponents = 1024;
    Resources.maxTessPatchComponents = 120;
    Resources.maxPatchVertices = 32;
    Resources.maxTessGenLevel = 64;
    Resources.maxViewports = 16;
    Resources.maxVertexAtomicCounters = 0;
    Resources.maxTessControlAtomicCounters = 0;
    Resources.maxTessEvaluationAtomicCounters = 0;
    Resources.maxGeometryAtomicCounters = 0;
    Resources.maxFragmentAtomicCounters = 8;
    Resources.maxCombinedAtomicCounters = 8;
    Resources.maxAtomicCounterBindings = 1;
    Resources.maxVertexAtomicCounterBuffers = 0;
    Resources.maxTessControlAtomicCounterBuffers = 0;
    Resources.maxTessEvaluationAtomicCounterBuffers = 0;
    Resources.maxGeometryAtomicCounterBuffers = 0;
    Resources.maxFragmentAtomicCounterBuffers = 1;
    Resources.maxCombinedAtomicCounterBuffers = 1;
    Resources.maxAtomicCounterBufferSize = 16384;
    Resources.maxTransformFeedbackBuffers = 4;
    Resources.maxTransformFeedbackInterleavedComponents = 64;
    Resources.maxCullDistances = 8;
    Resources.maxCombinedClipAndCullDistances = 8;
    Resources.maxSamples = 4;
    Resources.maxMeshOutputVerticesNV = 256;
    Resources.maxMeshOutputPrimitivesNV = 512;
    Resources.maxMeshWorkGroupSizeX_NV = 32;
    Resources.maxMeshWorkGroupSizeY_NV = 1;
    Resources.maxMeshWorkGroupSizeZ_NV = 1;
    Resources.maxTaskWorkGroupSizeX_NV = 32;
    Resources.maxTaskWorkGroupSizeY_NV = 1;
    Resources.maxTaskWorkGroupSizeZ_NV = 1;
    Resources.maxMeshViewCountNV = 4;
    Resources.limits.nonInductiveForLoops = 1;
    Resources.limits.whileLoops = 1;
    Resources.limits.doWhileLoops = 1;
    Resources.limits.generalUniformIndexing = 1;
    Resources.limits.generalAttributeMatrixVectorIndexing = 1;
    Resources.limits.generalVaryingIndexing = 1;
    Resources.limits.generalSamplerIndexing = 1;
    Resources.limits.generalVariableIndexing = 1;
    Resources.limits.generalConstantMatrixVectorIndexing = 1;
}

namespace {
    EShLanguage FindLanguage(const vk::ShaderStageFlagBits shader_type) {
        switch (shader_type) {
            case vk::ShaderStageFlagBits::eVertex: return EShLangVertex;
            case vk::ShaderStageFlagBits::eTessellationControl: return EShLangTessControl;
            case vk::ShaderStageFlagBits::eTessellationEvaluation: return EShLangTessEvaluation;
            case vk::ShaderStageFlagBits::eGeometry: return EShLangGeometry;
            case vk::ShaderStageFlagBits::eFragment: return EShLangFragment;
            case vk::ShaderStageFlagBits::eCompute: return EShLangCompute;
            default: return EShLangVertex;
        }
    }
};

bool SpirvHelper::GLSLtoSPV(const vk::ShaderStageFlagBits shader_type, const char* pshader, std::vector<unsigned int>& spirv, const bool targetVulkan) {
    const EShLanguage stage = FindLanguage(shader_type);
    glslang::TShader shader(stage);
    glslang::TProgram program;

    const char* shaderStrings[1];
    TBuiltInResource Resources = {};
    InitResources(Resources);

    // Enable SPIR-V and Vulkan rules when parsing GLSL
    const EShMessages messages = (EShMessages)(EShMsgSpvRules | (targetVulkan ? EShMsgVulkanRules : 0));

    shaderStrings[0] = pshader;
    shader.setStrings(shaderStrings, 1);
    shader.setEnvInput(glslang::EShSourceGlsl, stage, glslang::EShClientOpenGL, 100);
    shader.setEnvClient(glslang::EShClientOpenGL, glslang::EShTargetOpenGL_450);
    shader.setEnvTarget(glslang::EShTargetSpv, glslang::EShTargetSpv_1_4);
    if (!shader.parse(&Resources, 100, false, messages)) {
        Divide::Console::errorfn(shader.getInfoLog());
        Divide::Console::errorfn(shader.getInfoDebugLog());
        Divide::Console::toggleTextDecoration(false);
        Divide::Console::errorfn("-------------------------------------------------------\n\n");
        Divide::Console::errorfn(pshader);
        Divide::Console::errorfn("\n\n-------------------------------------------------------");
        Divide::Console::toggleTextDecoration(true);
        return false;  // something didn't work
    } else {
        if (strlen(shader.getInfoLog()) > 0) {
            Divide::Console::warnfn(shader.getInfoLog());
        }
        if (strlen(shader.getInfoDebugLog()) > 0) {
            Divide::Console::d_warnfn(shader.getInfoDebugLog());
        }
    }

    program.addShader(&shader);

    //
    // Program-level processing...
    //

    if (!program.link(messages)) {
        Divide::Console::errorfn(shader.getInfoLog());
        Divide::Console::errorfn(shader.getInfoDebugLog());
        Divide::Console::toggleTextDecoration(false);
        Divide::Console::errorfn("-------------------------------------------------------\n\n");
        Divide::Console::errorfn(pshader);
        Divide::Console::errorfn("\n\n-------------------------------------------------------");
        Divide::Console::toggleTextDecoration(true);
        Divide::Console::flush();
        return false;
    } else {
        if (strlen(shader.getInfoLog()) > 0) {
            Divide::Console::warnfn(shader.getInfoLog());
        }
        if (strlen(shader.getInfoDebugLog()) > 0) {
            Divide::Console::d_warnfn(shader.getInfoDebugLog());
        }
    }

    glslang::GlslangToSpv(*program.getIntermediate(stage), spirv);
    return !spirv.empty();
}

bool SpirvHelper::BuildReflectionData(const vk::ShaderStageFlagBits shader_type, const std::vector<unsigned int>& spirv, const bool targetVulkan, Divide::Reflection::Data& reflectionDataInOut) {
    const EShLanguage stage = FindLanguage(shader_type);

    switch (stage) {
        case EShLangVertex: reflectionDataInOut._stageVisibility |= to_base(Divide::ShaderStageVisibility::VERTEX); break;
        case EShLangTessControl: reflectionDataInOut._stageVisibility |= to_base(Divide::ShaderStageVisibility::TESS_CONTROL); break;
        case EShLangTessEvaluation: reflectionDataInOut._stageVisibility |= to_base(Divide::ShaderStageVisibility::TESS_EVAL); break;
        case EShLangGeometry: reflectionDataInOut._stageVisibility |= to_base(Divide::ShaderStageVisibility::GEOMETRY); break;
        case EShLangFragment: reflectionDataInOut._stageVisibility |= to_base(Divide::ShaderStageVisibility::FRAGMENT); break;
        case EShLangCompute: reflectionDataInOut._stageVisibility |= to_base(Divide::ShaderStageVisibility::COMPUTE); break;
        default: Divide::DIVIDE_UNEXPECTED_CALL(); break;
    };

    SpvReflectShaderModule module;
    SpvReflectResult result = spvReflectCreateShaderModule(spirv.size() * sizeof(unsigned int), spirv.data(), &module);
    if (result != SPV_REFLECT_RESULT_SUCCESS) {
        return false;
    }
    uint32_t count = 0u;
    result = spvReflectEnumerateDescriptorSets(&module, &count, NULL);
    if (result != SPV_REFLECT_RESULT_SUCCESS) {
        return false;
    }
    std::vector<SpvReflectDescriptorSet*> sets(count);
    result = spvReflectEnumerateDescriptorSets(&module, &count, sets.data());
    if (result != SPV_REFLECT_RESULT_SUCCESS) {
        return false;
    }

    const auto setResourceBinding = [targetVulkan](Divide::Reflection::DataEntry& entry, Divide::U8 bindingSet, Divide::U8 bindingSlot, const Divide::DescriptorSetBindingType type) {
        if (!targetVulkan) {
            assert(bindingSet == 0u);

            const auto[set, slot] = Divide::ShaderProgram::GetDescriptorSlotForGLBinding(bindingSlot, type);
            bindingSet = to_base(set);
            bindingSlot = slot;
        }

        entry._bindingSet = bindingSet;
        entry._bindingSlot = bindingSlot;
    };

    const auto fillImageData = [](Divide::Reflection::ImageEntry& target, const SpvReflectDescriptorBinding& binding) {
        const auto& traits = binding.image;
        target._isMultiSampled = traits.ms > 0u;
        target._isArray = traits.arrayed > 0u;
        if (binding.name != nullptr && strlen(binding.name) > 0) {
            target._name = binding.name;
        } else {
            Divide::DIVIDE_UNEXPECTED_CALL();
        }
    };
    std::function<void(Divide::Reflection::BufferMember&, const SpvReflectBlockVariable&)> fillBufferMemberData;
    fillBufferMemberData = [&fillBufferMemberData](Divide::Reflection::BufferMember& target, const SpvReflectBlockVariable& binding) {
        target._offset = binding.offset;
        target._absoluteOffset = binding.absolute_offset;
        target._size = binding.size;
        target._paddedSize = binding.padded_size;
        if (binding.array.dims_count > 0) {
            target._arrayInnerSize = binding.array.dims[0];
            if (binding.array.dims_count > 1) {
                target._arrayOuterSize = binding.array.dims[1];
            } 
            if (binding.array.dims_count > 2) {
                // We only support 2D arrays max
                Divide::DIVIDE_UNEXPECTED_CALL();
            }
        }
        target._vectorDimensions = binding.numeric.vector.component_count;
        target._matrixDimensions.x = binding.numeric.matrix.column_count;
        target._matrixDimensions.y = binding.numeric.matrix.row_count;
        if (binding.name != nullptr && strlen(binding.name) > 0) {
            target._name = binding.name;
        } else {
            Divide::DIVIDE_UNEXPECTED_CALL();
        }

        const uint32_t typeFlags = binding.type_description->type_flags;
        if (typeFlags & SpvReflectTypeFlagBits::SPV_REFLECT_TYPE_FLAG_INT) {
            if (binding.numeric.scalar.signedness == 1) {
                target._type = Divide::GFX::PushConstantType::INT;
            } else {
                target._type = Divide::GFX::PushConstantType::UINT;
            }
        } else if (typeFlags & SpvReflectTypeFlagBits::SPV_REFLECT_TYPE_FLAG_FLOAT) {
            Divide::DIVIDE_ASSERT(binding.numeric.scalar.signedness == 0);
            if (binding.numeric.scalar.width == 32) {
                target._type = Divide::GFX::PushConstantType::FLOAT;
            } else if (binding.numeric.scalar.width == 64) {
                target._type = Divide::GFX::PushConstantType::DOUBLE;
            } else {
                Divide::DIVIDE_UNEXPECTED_CALL();
            }
        } else if (typeFlags & SpvReflectTypeFlagBits::SPV_REFLECT_TYPE_FLAG_VECTOR) {
            Divide::DIVIDE_UNEXPECTED_CALL();
        } else if (typeFlags & SpvReflectTypeFlagBits::SPV_REFLECT_TYPE_FLAG_MATRIX) {
            Divide::DIVIDE_UNEXPECTED_CALL();
        } else if (typeFlags & SpvReflectTypeFlagBits::SPV_REFLECT_TYPE_FLAG_BOOL) {
            Divide::DIVIDE_UNEXPECTED_CALL();
        } else if (typeFlags & SpvReflectTypeFlagBits::SPV_REFLECT_TYPE_FLAG_STRUCT) {
            target._memberCount = binding.member_count;
            target._members.resize(target._memberCount);
            for (size_t i = 0u; i < target._memberCount; ++i) {
                fillBufferMemberData(target._members[i], binding.members[i]);
            }
        } else if (typeFlags & SpvReflectTypeFlagBits::SPV_REFLECT_TYPE_FLAG_ARRAY) {
            Divide::DIVIDE_UNEXPECTED_CALL();
        }
    };

    const auto fillBufferData = [&](Divide::Reflection::BufferEntry& target, const SpvReflectDescriptorBinding& binding) {
        const SpvReflectBlockVariable& sourceBlock = binding.block;
        target._offset = sourceBlock.offset;
        target._absoluteOffset = sourceBlock.absolute_offset;
        target._size = sourceBlock.size;
        target._paddedSize = sourceBlock.padded_size;
        target._memberCount = sourceBlock.member_count;
        if (binding.type_description != nullptr && binding.type_description->type_name != nullptr) {
            target._name = binding.type_description->type_name;
        } else if (binding.name != nullptr && strlen(binding.name) > 0) {
            target._name = binding.name;
        } else {
            Divide::DIVIDE_UNEXPECTED_CALL();
        }
        target._members.resize(target._memberCount);
        for (uint32_t i = 0u; i < target._memberCount; ++i) {
            fillBufferMemberData(target._members[i], sourceBlock.members[i]);
        }
    };

    for (uint32_t i = 0u; i < count; ++i) {
        const SpvReflectDescriptorSet& refl_set = *(sets[i]);
        for (uint32_t i_binding = 0u; i_binding < refl_set.binding_count; ++i_binding) {
            const SpvReflectDescriptorBinding& refl_binding = *(refl_set.bindings[i_binding]);

            if (refl_binding.descriptor_type == SPV_REFLECT_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER) {
                Divide::Reflection::ImageEntry& image = reflectionDataInOut._images.emplace_back();
                image._combinedImageSampler = true;
                image._isWriteTarget = false;
                fillImageData(image, refl_binding);
                setResourceBinding(image, Divide::to_U8(refl_binding.set), Divide::to_U8(refl_binding.binding), Divide::DescriptorSetBindingType::COMBINED_IMAGE_SAMPLER);
            } else if (refl_binding.descriptor_type == SPV_REFLECT_DESCRIPTOR_TYPE_SAMPLED_IMAGE) {
                Divide::Reflection::ImageEntry& image = reflectionDataInOut._images.emplace_back();
                image._combinedImageSampler = false;
                image._isWriteTarget = false;
                fillImageData(image, refl_binding);
                setResourceBinding(image, Divide::to_U8(refl_binding.set), Divide::to_U8(refl_binding.binding), Divide::DescriptorSetBindingType::COMBINED_IMAGE_SAMPLER);
            } else if (refl_binding.descriptor_type == SPV_REFLECT_DESCRIPTOR_TYPE_SAMPLER) {
                Divide::DIVIDE_UNEXPECTED_CALL(); //Not yet supported!
            } else if (refl_binding.descriptor_type == SPV_REFLECT_DESCRIPTOR_TYPE_STORAGE_IMAGE) {
                Divide::Reflection::ImageEntry& image = reflectionDataInOut._images.emplace_back();
                image._combinedImageSampler = false;
                image._isWriteTarget = true;
                fillImageData(image, refl_binding);
                setResourceBinding(image, Divide::to_U8(refl_binding.set), Divide::to_U8(refl_binding.binding), Divide::DescriptorSetBindingType::IMAGE);
            } else if (refl_binding.descriptor_type == SPV_REFLECT_DESCRIPTOR_TYPE_UNIFORM_BUFFER) {
                Divide::Reflection::BufferEntry& buffer = reflectionDataInOut._buffers.emplace_back();
                buffer._uniformBuffer = true;
                buffer._dynamic = false;
                fillBufferData(buffer, refl_binding);
                setResourceBinding(buffer, Divide::to_U8(refl_binding.set), Divide::to_U8(refl_binding.binding), Divide::DescriptorSetBindingType::UNIFORM_BUFFER);
            } else if (refl_binding.descriptor_type == SPV_REFLECT_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC) {
                Divide::Reflection::BufferEntry& buffer = reflectionDataInOut._buffers.emplace_back();
                buffer._uniformBuffer = true;
                buffer._dynamic = true;
                fillBufferData(buffer, refl_binding);
                setResourceBinding(buffer, Divide::to_U8(refl_binding.set), Divide::to_U8(refl_binding.binding), Divide::DescriptorSetBindingType::UNIFORM_BUFFER);
            } else if (refl_binding.descriptor_type == SPV_REFLECT_DESCRIPTOR_TYPE_STORAGE_BUFFER) {
                Divide::Reflection::BufferEntry& buffer = reflectionDataInOut._buffers.emplace_back();
                buffer._uniformBuffer = false;
                buffer._dynamic = false;
                fillBufferData(buffer, refl_binding);
                setResourceBinding(buffer, Divide::to_U8(refl_binding.set), Divide::to_U8(refl_binding.binding), Divide::DescriptorSetBindingType::SHADER_STORAGE_BUFFER);
            } else if (refl_binding.descriptor_type == SPV_REFLECT_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC) {
                Divide::Reflection::BufferEntry& buffer = reflectionDataInOut._buffers.emplace_back();
                buffer._uniformBuffer = false;
                buffer._dynamic = true;
                fillBufferData(buffer, refl_binding);
                setResourceBinding(buffer, Divide::to_U8(refl_binding.set), Divide::to_U8(refl_binding.binding), Divide::DescriptorSetBindingType::SHADER_STORAGE_BUFFER);
            } else {
                Divide::DIVIDE_UNEXPECTED_CALL();
            }
        }
    }

    spvReflectDestroyShaderModule(&module);

    return true;
}
