#include "stdafx.h"

#include "Headers/GLSLToSPIRV.h"
#include "Platform/Video/Shaders/Headers/ShaderProgram.h"

#include <glslang/SPIRV/GlslangToSpv.h>
#include <Vulkan/vulkan.hpp>

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

bool SpirvHelper::GLSLtoSPV(const vk::ShaderStageFlagBits shader_type, const char* pshader, std::vector<unsigned int>& spirv, const bool targetVulkan, Divide::Reflection::Data& reflectionDataInOut) {
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

    BuildReflectionData(program, shader_type, reflectionDataInOut);

    return true;
}

void SpirvHelper::BuildReflectionData(glslang::TProgram& program, const vk::ShaderStageFlagBits shader_type, Divide::Reflection::Data& reflectionDataInOut) {
    if (reflectionDataInOut._blockSize != 0u) {
        return;
    }

    Divide::DIVIDE_ASSERT(reflectionDataInOut._blockMembers.empty());

    if (program.buildReflection()) {
        const int numUniformBlocks = program.getNumLiveUniformBlocks();
        for (int i = 0; i < numUniformBlocks; ++i) {
            const auto& block = program.getUniformBlock(i);

            if (block.name == reflectionDataInOut._targetBlockName) {
                reflectionDataInOut._blockSize = block.size;
                const auto& structure = block.getType()->getStruct();
                const int numMembers = block.numMembers;
                reflectionDataInOut._blockMembers.resize(numMembers);
                for (int j = 0; j < numMembers; ++j) {
                    const auto& member = (*structure)[j];
                    Divide::Reflection::BlockMember& target = reflectionDataInOut._blockMembers[j];

                    target._offset = member.type->getQualifier().layoutOffset;
                    target._type = GetGFXType(member.type->getBasicType());
                    target._vectorDimensions = member.type->getVectorSize();
                    target._matrixDimensions = { member.type->getMatrixCols(), member.type->getMatrixRows() };
                    target._name = member.type->getFieldName().c_str();
                    if (member.type->isArray()) {
                        target._arrayOuterSize = member.type->getOuterArraySize();
                        const int numDimemsions = member.type->getArraySizes()->getNumDims();
                        Divide::DIVIDE_ASSERT(numDimemsions <= 2);
                        if (numDimemsions > 1) {
                            target._arrayInnerSize = member.type->getArraySizes()->getDimSize(1);
                        }
                    }
                }
            }
        }

        if (shader_type == vk::ShaderStageFlagBits::eVertex) {
            reflectionDataInOut._enabledAttributes.fill(false);
            const int numAttribInputs = program.getNumLiveUniformBlocks();
            for (int i = 0; i < numAttribInputs; ++i) {
                const auto& attrib = program.getPipeInput(i);
                const int binding = attrib.getBinding();
                if (binding != -1) {
                    reflectionDataInOut._enabledAttributes[binding] = true;
                }
            }
            Divide::NOP();
        }
    }
}
